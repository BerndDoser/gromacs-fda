#!/bin/bash

declare -a arr=(
  "alagly_pairwise_forces_scalar_summed"
  "alagly_pairwise_forces_scalar_summed_renumbered"
  "alagly_pairwise_forces_scalar_summed_atom_based"
  "alagly_pairwise_forces_scalar_summed_no_residue_based"
  "alagly_pairwise_forces_scalar_threshold"
  "alagly_pairwise_forces_scalar_average_0"
  "alagly_pairwise_forces_scalar_average_3"
  "alagly_pairwise_forces_scalar_average_0_threshold"
  "alagly_pairwise_forces_scalar_detailed"
  "alagly_pairwise_forces_vector"
  "alagly_pairwise_forces_scalar_detailed_nonbonded"
  "alagly_pairwise_forces_vector_detailed_nonbonded"
  "alagly_pairwise_forces_scalar_all"
  "alagly_punctual_stress"
  "alagly_punctual_stress_binary"
  "alagly_punctual_stress_normalized"
  "alagly_punctual_stress_normalized_renumbered"
  "glycine_trimer_virial_stress"
  "glycine_trimer_virial_stress_von_mises"
)

declare -a files=(
  "pfa"
  "pfr"
  "psa"
  "psr"
  "vsa"
  "vma"
)

for i in "${arr[@]}"
do
  for j in "${files[@]}"
  do
    echo "cp $1/*/$i/fda.$j $2/$i/fda.$j.ref"
    cp $1/*/$i/fda.$j $2/$i/fda.$j.ref
  done
done
