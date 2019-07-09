
#include <new>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <iomanip>

#ifndef WIN32
#include <unistd.h>
#endif

#include <AMReX_CArena.H>
#include <AMReX_REAL.H>
#include <AMReX_Utility.H>
#include <AMReX_IntVect.H>
#include <AMReX_Box.H>
#include <AMReX_Amr.H>
#include <AMReX_ParmParse.H>
#include <AMReX_ParallelDescriptor.H>
#include <AMReX_AmrLevel.H>

#include <time.h>

#include <Castro.H>
#include <Castro_io.H>

using namespace amrex;

std::string inputs_name = "";

int
main (int   argc,
      char* argv[])
{

    amrex::Initialize(argc,argv);

    if (argc <= 1)
	amrex::Abort("Error: no inputs file provided on command line.");

    // Save the inputs file name for later.

    if (!strchr(argv[1], '=')) {
	inputs_name = argv[1];
    }

    BL_PROFILE_VAR("main()", pmain);

    Real dRunTime1 = ParallelDescriptor::second();

    int max_step = 10000000;
    Real stop_time = 1.0e-2;

    ParmParse pp;
    pp.query("max_step",max_step);
    pp.query("stop_time",stop_time);

    // Set the geometry parameters for this problem.
    // They are hardcoded for the Sedov blast wave
    // that we are solving.

    ParmParse pp_geom("geometry");

    std::vector<int> periodic{1, 1, 1};
    std::vector<Real> prob_lo{0.0, 0.0, 0.0};
    std::vector<Real> prob_hi{1.0e9, 1.0e9, 1.0e9};

    pp_geom.add("coord_sys", 0);
    pp_geom.addarr("is_periodic", periodic);
    pp_geom.addarr("prob_lo", prob_lo);
    pp_geom.addarr("prob_hi", prob_hi);

    // Use n_cell to replace amr.n_cell for a friendlier
    // user experience for those unfamiliar with AMReX.

    ParmParse pp_amr("amr");

    int n_cell = 128;
    pp.query("n_cell", n_cell);
    std::vector<int> n_cell_arr{n_cell, n_cell, n_cell};
    pp_amr.addarr("n_cell", n_cell_arr);

    // Use max_grid_size to replace amr.max_grid_size.

    int max_grid_size = 64;
    pp.query("max_grid_size", max_grid_size);
    pp_amr.add("max_grid_size", max_grid_size);

    // Use min_grid_size to replace amr.blocking_factor.

    int min_grid_size = 16;
    pp.query("min_grid_size", min_grid_size);
    pp_amr.add("blocking_factor", min_grid_size);

    Amr* amrptr = new Amr;

    amrptr->init(0.0, stop_time);

    Real dRunTime2 = ParallelDescriptor::second();

    while ( amrptr->okToContinue()                            &&
           (amrptr->levelSteps(0) < max_step || max_step < 0) &&
           (amrptr->cumTime() < stop_time || stop_time < 0.0) )
    {
        //
        // Do a timestep.
        //
        amrptr->coarseTimeStep(stop_time);

    }

    delete amrptr;

    const int IOProc = ParallelDescriptor::IOProcessorNumber();

    Real dRunTime3 = ParallelDescriptor::second();

    Real runtime_total = dRunTime3 - dRunTime1;
    Real runtime_timestep = dRunTime3 - dRunTime2;

    ParallelDescriptor::ReduceRealMax(runtime_total,IOProc);
    ParallelDescriptor::ReduceRealMax(runtime_timestep,IOProc);

    if (ParallelDescriptor::IOProcessor())
    {
	int nProcs = ParallelDescriptor::NProcs();
#ifdef _OPENMP
	nProcs *= omp_get_max_threads();
#endif
	Real fom = Castro::num_zones_advanced / runtime_timestep / 1.e6;

        amrex::Print() << std::endl;
        amrex::Print() << "  Figure of Merit (zones / usec): " << std::fixed << std::setprecision(3) << fom << "\n";
        amrex::Print() << std::endl;
    }

    BL_PROFILE_VAR_STOP(pmain);
    BL_PROFILE_SET_RUN_TIME(dRunTime2);

    amrex::Finalize();

    return 0;
}
