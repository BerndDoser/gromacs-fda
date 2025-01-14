/*
 * gmx_fda_graph.cpp
 *
 *  Created on: Feb 13, 2015
 *      Author: Bernd Doser, HITS gGmbH <bernd.doser@h-its.org>
 */

#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include "fda/EnumParser.h"
#include "fda/FrameType.h"
#include "fda/Graph.h"
#include "fda/Helpers.h"
#include "fda/ResultFormat.h"
#include "gmx_ana.h"
#include "gromacs/commandline/filenm.h"
#include "gromacs/commandline/pargs.h"
#include "gromacs/fda/PairwiseForces.h"
#include "gromacs/fileio/confio.h"
#include "gromacs/fileio/oenv.h"
#include "gromacs/fileio/pdbio.h"
#include "gromacs/fileio/tpxio.h"
#include "gromacs/fileio/trxio.h"
#include "gromacs/math/vec.h"
#include "gromacs/math/vectypes.h"
#include "gromacs/topology/index.h"
#include "gromacs/topology/topology.h"
#include "gromacs/utility/arraysize.h"
#include "gromacs/utility/basedefinitions.h"
#include "gromacs/utility/fatalerror.h"
#include "gromacs/utility/futil.h"
#include "gromacs/utility/real.h"

#ifdef HAVE_CONFIG_H
  #include <config.h>
#endif

using namespace fda_analysis;

#define PRINT_DEBUG

int gmx_fda_graph(int argc, char *argv[])
{
    const char *desc[] = {
        "[THISMODULE] converts a FDA force network into a PDB or DIMACS graph. "
        "If the optional file [TT]-diff[tt] is used the differences of the pairwise forces will be taken. "
        "The PDB graph allows an easy visualization with a program of your choice. "
        "The option [TT]-pymol[tt] can be used to generate a Pymol script, which can be directly called by Pymol. "
        "Only forces larger than the [TT]-t[tt] will be considered. The default threshold is zero. "
        "Networks must contains at least the same number of nodes as the the min-value (default: 2). "
        "If the option [TT]-big[tt] is used, only the biggest network in term of number of nodes will be printed. "
        "Each network will be determined and segment names will be assign to each "
        "of them, thus coloring them by segment id will help the analysis "
        "(32 different colors). The Bfactor column will be used for the value of "
        "the force and helps the coloring as a function of the force magnitude. "
        "The CONNECT header will be used to create bonds between nodes. "
    };

    gmx_output_env_t *oenv;
    real threshold = 0.0;
    const char* frameString = "average 1";
    int minGraphOrder = 2;
    bool onlyBiggestNetwork = false;
    bool convert = false;

    t_pargs pa[] = {
        { "-frame", FALSE, etSTR, {&frameString}, "Specify a single frame number or \"average n\" to take the mean over every n-th frame"
              " or \"skip n\" to take every n-th frame or \"all\" to take all frames (e.g. for movies)" },
        { "-t", FALSE, etREAL, {&threshold}, "Threshold for neglecting forces lower than this value" },
        { "-min", FALSE, etINT, {&minGraphOrder}, "Minimal size of the networks" },
        { "-big", FALSE, etBOOL, {&onlyBiggestNetwork}, "If True, export only the biggest network" },
        { "-convert", FALSE, etBOOL, {&convert}, "Convert force unit from kJ/mol/nm into pN" }
    };

    t_filenm fnm[] = {
        { efPFX, "-i", nullptr, ffREAD },
        { efPFX, "-diff", nullptr, ffOPTRD },
        { efTPS, nullptr, nullptr, ffREAD },
        { efTRX, "-f", nullptr, ffOPTRD },
        { efNDX, nullptr, nullptr, ffOPTRD },
        { efGRX, "-o", "result", ffWRITE },
        { efPML, "-pymol", "result", ffOPTWR }
    };

#define NFILE asize(fnm)

    if (!parse_common_args(&argc, argv, PCA_CAN_TIME,
        NFILE, fnm, asize(pa), pa, asize(desc), desc, 0, nullptr, &oenv)) return 0;

    if (opt2bSet("-diff", NFILE, fnm) and (fn2ftp(opt2fn("-diff", NFILE, fnm)) != fn2ftp(opt2fn("-i", NFILE, fnm))))
        gmx_fatal(FARGS, "Type of the file (-diff) does not match the type of the file (-i).");

    if (fn2ftp(opt2fn("-i", NFILE, fnm)) == efPFR and !opt2bSet("-n", NFILE, fnm))
        gmx_fatal(FARGS, "Index file is needed for residuebased pairwise forces.");

    // Open pairwise forces file
    fda::PairwiseForces<fda::Force<real>> pairwise_forces(opt2fn("-i", NFILE, fnm));
    std::shared_ptr<fda::PairwiseForces<fda::Force<real>>> ptr_pairwise_forces_diff;
    if (opt2bSet("-diff", NFILE, fnm)) {
    	ptr_pairwise_forces_diff = std::make_shared<fda::PairwiseForces<fda::Force<real>>>(opt2fn("-diff", NFILE, fnm));
    }

    // Get number of particles
    int nbParticles = pairwise_forces.get_max_index_second_column_first_frame() + 1;
    int nbParticles2 = nbParticles * nbParticles;

    // Interactive input of group name for residue model points
    int isize = 0;
    int *index = nullptr;
    char *grpname;
    if (ftp2bSet(efNDX, NFILE, fnm)) {
        fprintf(stderr, "\nSelect group for residue model points:\n");
        rd_index(ftp2fn(efNDX, NFILE, fnm), 1, &isize, &index, &grpname);
    }

    int frameValue;
    FrameType frameType = getFrameTypeAndSkipValue(frameString, frameValue);

    ResultFormat resultFormat = UNKNOWN;
    if (fn2ftp(opt2fn("-o", NFILE, fnm)) == efPDB) resultFormat = PDB;
    else if (fn2ftp(opt2fn("-o", NFILE, fnm)) == efDIM) resultFormat = DIMACS;

    #ifdef PRINT_DEBUG
        std::cerr << "frameType = " << EnumParser<FrameType>()(frameType) << std::endl;
        std::cerr << "frameValue = " << frameValue << std::endl;
        std::cerr << "Number of particles (np) = " << nbParticles << std::endl;
        std::cerr << "threshold = " << threshold << std::endl;
        std::cerr << "minGraphOrder = " << minGraphOrder << std::endl;
        std::cerr << "onlyBiggestNetwork = " << onlyBiggestNetwork << std::endl;
        std::cerr << "convert = " << convert << std::endl;
        std::cerr << "pfx filename = " << opt2fn("-i", NFILE, fnm) << std::endl;
        if (opt2bSet("-diff", NFILE, fnm)) std::cerr << "diff filename = " << opt2fn("-diff", NFILE, fnm) << std::endl;
        std::cerr << "structure filename = " << opt2fn("-s", NFILE, fnm) << std::endl;
        std::cerr << "result filename = " << opt2fn("-o", NFILE, fnm) << std::endl;
        std::cerr << "result format = " << EnumParser<ResultFormat>()(resultFormat) << std::endl;
        if (opt2bSet("-pymol", NFILE, fnm)) std::cerr << "pymol = " << opt2fn("-pymol", NFILE, fnm) << std::endl;
    #endif

    // Read input structure coordinates
    rvec *coord;
    t_topology top;
    PbcType ePBC;
    matrix box;
    read_tps_conf(ftp2fn(efTPS, NFILE, fnm), &top, &ePBC, &coord, nullptr, box, TRUE);

    std::vector<double> forceMatrix, forceMatrix2;

    // Pymol pml-file
    std::string molecularTrajectoryFilename = "traj.pdb";
    FILE *molecularTrajectoryFile = nullptr;
    if (opt2bSet("-pymol", NFILE, fnm)) {
        if (resultFormat != PDB) gmx_fatal(FARGS, "Pymol result file makes only sense using pdb output format.");
        std::ofstream pmlFile(opt2fn("-pymol", NFILE, fnm));
        pmlFile << "load " << molecularTrajectoryFilename << ", object=trajectory" << std::endl;
        pmlFile << "set connect_mode, 1" << std::endl;
        pmlFile << "load " << opt2fn("-o", NFILE, fnm) << ", object=network, discrete=1, multiplex=1" << std::endl;
        pmlFile << "spectrum segi, blue_white_red, network" << std::endl;
        pmlFile << "show_as lines, trajectory" << std::endl;
        pmlFile << "show_as sticks, network" << std::endl;
        molecularTrajectoryFile = gmx_ffopen(molecularTrajectoryFilename.c_str(), "w");
    }

    if (frameType == SINGLE) {

        forceMatrix = pairwise_forces.get_forcematrix_of_frame(nbParticles, frameValue);
        if (opt2bSet("-diff", NFILE, fnm)) {
            forceMatrix2 = ptr_pairwise_forces_diff->get_forcematrix_of_frame(nbParticles, frameValue);
        }

        if (opt2bSet("-diff", NFILE, fnm)) for (int i = 0; i < nbParticles2; ++i) forceMatrix[i] -= forceMatrix2[i];
        for (auto & f : forceMatrix) f = std::abs(f);

        // Convert from kJ/mol/nm into pN
        if (convert) for (auto & f : forceMatrix) f *= 1.66;

        std::cout << "isize " << isize << std::endl;
        std::cout << "index ";
        for (int i = 0; i != isize; ++i) {
            std::cout << index[i] << " ";
        }
        std::cout << std::endl;
        Graph graph(forceMatrix, coord, index, isize);
        std::cout << graph << std::endl;

        if (resultFormat == PDB)
            graph.convertInPDBMinGraphOrder(opt2fn("-o", NFILE, fnm), threshold, minGraphOrder, onlyBiggestNetwork, false);
        else if (resultFormat == DIMACS)
            graph.convertInDIMACSMinGraphOrder(opt2fn("-o", NFILE, fnm), threshold, minGraphOrder, onlyBiggestNetwork);

    } else {

        if (resultFormat == DIMACS) gmx_fatal(FARGS, "DIMACS format is not supported for multiple frames.");

        // Read trajectory coordinates
        t_trxstatus *status;
        real time;
        rvec *coord_traj;
        matrix box;

        int nbFrames = pairwise_forces.get_number_of_frames();
        for (int frame = 0; frame < nbFrames; ++frame)
        {
            if (frame == 0) read_first_x(oenv, &status, opt2fn("-f", NFILE, fnm), &time, &coord_traj, box);
            else read_next_x(oenv, status, &time, coord_traj, box);

            if (frameType == SKIP and frame%frameValue) continue;

            forceMatrix = pairwise_forces.get_forcematrix_of_frame(nbParticles, frame);
            if (opt2bSet("-diff", NFILE, fnm)) {
                forceMatrix2 = ptr_pairwise_forces_diff->get_forcematrix_of_frame(nbParticles, frame);
                for (int i = 0; i < nbParticles2; ++i) forceMatrix[i] -= forceMatrix2[i];
            }
            for (auto & f : forceMatrix) f = std::abs(f);

            // Convert from kJ/mol/nm into pN
            if (convert) for (auto & f : forceMatrix) f *= 1.66;

            if (frameType == AVERAGE) {
                for (int frameAvg = 0; frameAvg < frameValue - 1; ++frameAvg)
                {
                    std::vector<double> forceMatrixAvg, forceMatrixAvg2;
                    forceMatrixAvg = pairwise_forces.get_forcematrix_of_frame(nbParticles, frame);
                    if (opt2bSet("-diff", NFILE, fnm)) {
                        forceMatrixAvg2 = ptr_pairwise_forces_diff->get_forcematrix_of_frame(nbParticles, frame);
                        for (int i = 0; i < nbParticles2; ++i) forceMatrixAvg[i] -= forceMatrixAvg2[i];
                    }

                    for (auto & f : forceMatrixAvg) f = std::abs(f);

                    // Convert from kJ/mol/nm into pN
                    if (convert) for (auto & f : forceMatrixAvg) f *= 1.66;

                    for (int i = 0; i < nbParticles2; ++i) forceMatrix[i] += forceMatrixAvg[i];
                }
                for (int i = 0; i < nbParticles2; ++i) forceMatrix[i] /= frameValue;
            }

            Graph graph(forceMatrix, coord_traj, index, isize);
            graph.convertInPDBMinGraphOrder(opt2fn("-o", NFILE, fnm), threshold, minGraphOrder, onlyBiggestNetwork, frame);

            // Write moleculare trajectory for pymol script
            if (opt2bSet("-pymol", NFILE, fnm))
                write_pdbfile(molecularTrajectoryFile, "FDA trajectory for Pymol visualization", &top.atoms, coord_traj, ePBC, box, ' ', 0, nullptr);

            if (frameType == AVERAGE) {
                frame += frameValue - 1;
                // It would be better to skip the next frameValue - 1 frames instead of reading them.
                for (int frameAvg = 0; frameAvg < frameValue - 1; ++frameAvg) {
                    read_next_x(oenv, status, &time, coord_traj, box);
                }
            }
        }
        close_trx(status);
    }

    if (opt2bSet("-pymol", NFILE, fnm)) gmx_ffclose(molecularTrajectoryFile);

    std::cout << "All done." << std::endl;
    return 0;

}
