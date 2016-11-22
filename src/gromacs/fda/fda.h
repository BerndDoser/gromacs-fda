/*
 * Arrays of pairwise forces. This is a compromise between memory required to
 * store the pairwise forces and the CPU time needed to access them (insert,
 * lookup).
 * For each atom i there is a struct containing several arrays; there are
 * separate arrays for each interaction type, so there is no need to keep an
 * extra 'type' field. Each interaction takes space for one atom index and a
 * real vector containing the value of the force.
 * The arrays are grown as needed, through memory reallocations, similar to the
 * nonbonded lists; to avoid too many reallocations (memory management can be
 * expensive especially with certain MPI implementations which register memory
 * to achieve zero copy), they are always done by increasing the current size
 * with a factor > 1. However, this factor should be kept small enough to
 * avoid allocating too much memory which will then remain unused.
 * For each array there is a counter for the number of items currently in use
 * and one for the number of items for which space was allocated. If they are
 * equal and there is a request for storing another interaction, a reallocation
 * will happen.
 * Lookup is done through a linear search in the array. This uses CPU time, but
 * saves memory - organizing the items in a binary tree would require 2
 * pointers per interaction and allocations cannot be done in bulk anymore.
 * Insertion can be done directly at the end if it's known that the jjnr does
 * not already exist in the list; otherwise a linear search is needed.
 *
 * For the case of summed up forces per pair, a different set of data structures
 * are needed; the main reason is the extra data present in the summed up
 * interaction structure (the type) - it makes no sense to use a single structure
 * and ignore this field for the detailed storage of interactions, as this will
 * increase significantly the storage requirements. Furthermore, when storing
 * the atoms in group1, it makes no sense to keep all those NULL pointers
 * towards lists of interactions.
 * 
 * NOTE: all structures are defined for atoms, but are then used for both atoms and residues;
 * it made no sense to define new ones as they are functionally the same
 * 
 * Copyright Bogdan Costescu 2010-2013
 */

#ifndef SRC_GROMACS_FDA_FDA_H
#define SRC_GROMACS_FDA_FDA_H

#include "gromacs/utility/basedefinitions.h"

#ifdef __cplusplus
#include <cstdio>
#include <vector>
#include "FDABase.h"
#include "FDASettings.h"
#include "Tensor.h"
#endif

namespace fda {

#ifdef __cplusplus
class FDA {
public:

  /// Default constructor
  FDA(FDASettings const& fda_settings = FDASettings());

  /**
   * Checking is symmetrical for atoms i and j; one of them has to be from g1, the other one from g2;
   * the check below makes the atoms equivalent, make them always have the same order (i,j) and not (j,i) where i < j;
   * force is the force atom j exerts on atom i; if i and j are switched, the force goes in opposite direction
   * it's possible that i > j, but ri < rj, so the force has to be handled separately for each of them
   *
   * the logic is a bit complicated by the fact that atom_based_result_type and residue_based_result_type are independent;
   * if residue_based_result_type part is done first, the atom_based_result_type part can use i/j/force directly, without saving them
   * first in intermediate variables, as the initial values of i/j/force are no longer needed; if atom_based_result_type
   * is done first (the original code), i/j/force are needed for the later atom->residue mapping
   * and saving in intermediate variables is needed
   */
  void add_bonded_nocheck(int i, int j, int type, rvec force);

  void add_bonded(int i, int j, int type, rvec force);

  /**
   * Add a particular type of nonbonded interaction for the kernels where only one type of interaction is computed;
   * force is passed as scalar along with the distance vector (as dx, dy, dz) from which the vector force is
   * computed, the same way it's done in the nonbonded kernels
   */
  void add_nonbonded_single(int i, int j, int type, real force, real dx, real dy, real dz);

  /**
   * Add a nonbonded interaction for kernels where both Coulomb and LJ are computed;
   * this is more efficient than calling the previous one twice because some of the tests are made only once;
   * forces are passed as scalars along with the distance vector (as dx, dy, dz) from which the vector forces are
   * computed, the same way it's done in the nonbonded kernels
   */
  void add_nonbonded(int i, int j, real pf_coul, real pf_lj, real dx, real dy, real dz);

  void add_angle(int ai, int aj, int ak, rvec f_i, rvec f_j, rvec f_k);

  void add_dihedral(int i, int j, int k, int l, rvec f_i, rvec f_j, rvec f_k, rvec f_l);

  /**
   * The atom virial can be expressed as a 6-real tensor, as it's symmetric.
   * To avoid defining a new tensor type, the 9-real tensor is used instead.
   */
  void add_virial(int ai, tensor v, real s);

  /**
   * Origin on j, but for 2 atoms it doesn't matter.
   */
  void add_virial_bond(int ai, int aj, real fbond, real dx, real dy, real dz);

  /**
   * Translate to origin on the middle (j) atom:
   * vir = ri*Fi + rj*Fj + rk*Fk
   *     = (ri-rj)*Fi + (rk-rj)*Fk
   *     = r_ij[dim1]*f_i[dim2] + r_kj[dim1]*f_k[dim2]
   */
  void add_virial_angle(int ai, int aj, int ak, rvec r_ij, rvec r_kj, rvec f_i, rvec f_k);

  /**
   * Translate to origin on the second (j) atom:
   * vir = ri*Fi + rj*Fj + rk*Fk + rl*Fl
   *     = (ri-rj)*Fi + (rk-rj)*Fk + (rl-rj)*Fl
   *     = (ri-rj)*Fi + (rk-rj)*Fk + ((rl-rk) + (rk-rj))*Fl
   *     = r_ij[dim1]*f_i[dim2] + r_kj[dim1]*f_k[dim2] + (r_kj-r_kl)[dim1]*f_l[dim2]
   */
  void add_virial_dihedral(int i, int j, int k, int l, rvec f_i, rvec f_k, rvec f_l, rvec r_ij, rvec r_kj, rvec r_kl);

  void write_frame(const rvec *x,  gmx_mtop_t *top_global) /* const */;

  void write_frame_detailed(DistributedForces const& forces, FILE* f, int *framenr, const rvec *x, gmx_bool bVector, Vector2Scalar v2s) /* const */;

  void write_frame_summed(DistributedForces const& forces, FILE* f, int *framenr, const rvec *x, gmx_bool bVector, Vector2Scalar v2s) /* const */;

  void write_frame_scalar(DistributedForces const& forces, FILE* f, int *framenr) /* const */;

  void write_frame_atoms_scalar_compat(DistributedForces const& forces, FILE *f, int *framenr, gmx_bool ascii) /* const */;

  /**
   * Main function for scalar time averages; saves data and decides when to write it out
   *
   * dealing with residues is more complicated because COMs have to be averaged over time;
   * it's not possible to average the atom positions and calculate COMs only once before
   * writing because for this fda->atoms would need to be initialized (to get the atom
   * number or to get the sys2ps mapping) which only happens when AtomBased is non-zero
   */
  void save_and_write_scalar_time_averages(rvec const *x, gmx_mtop_t *top_global);

  /**
   * Write scalar time averages; this is similar to pf_write_frame, except that time averages are used
   *
   * There are several cases:
   * steps == 0 - there was no frame saved at all or there are no frames saved since the last writing, so no writing needed
   * steps > 0, period == 0 - no frames written so far, but frames saved, so writing one frame
   * steps > 0, period != 0 - frames saved, so writing the last frame
   *
   * if this is called from pf_write_and_save... then steps is certainly > 0
   * period is certainly != 1, otherwise the averages functions are not called
   */
  void write_scalar_time_averages();

  void per_atom_real_write_frame(FILE *f, std::vector<real> const& force_per_node, bool no_end_zeros);

  void per_atom_sum(std::vector<real>& force_per_node, DistributedForces& forces, int atoms_len, const rvec *x, Vector2Scalar v2s);

  /// The stress is the negative atom_vir value.
  void write_atom_virial_sum(FILE *f, tensor *atom_vir, int natoms);

  /// For von Mises no negative values are needed, since all items are squared.
  void write_atom_virial_sum_von_mises(FILE *f, tensor *atom_vir, int natoms);

private:

  /// Settings
  FDASettings const& fda_settings;

  /// Atom-based operation
  FDABase<Atom> atom_based;

  /// Residue-based operation
  FDABase<Residue> residue_based;

  /// Counter for current step, incremented for every call of pf_save_and_write_scalar_averages()
  /// When it reaches time_averages_steps, data is written
  int time_averaging_steps;

  /// Averaged residue COM coordinates over steps, needed for COM calculations
  /// Only initialized when residue_based_forces is non-zero
  rvec *time_averaging_com;

  /// For virial stress
  std::vector<Tensor> atom_vir;

#else
struct FDA {
#endif
};

} // namespace fda

#endif // SRC_GROMACS_FDA_FDA_H