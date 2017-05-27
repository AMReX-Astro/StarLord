
#include "Castro.H"
#include "Castro_F.H"

#include <cmath>
#include <climits>

using std::string;
using namespace amrex;

Real
Castro::advance (Real time,
                 Real dt,
                 int  amr_iteration,
                 int  amr_ncycle)

  // the main driver for a single level.  This will do either the SDC
  // algorithm or the Strang-split reactions algorithm.
  //
  // arguments:
  //    time          : the current simulation time
  //    dt            : the timestep to advance (e.g., go from time to 
  //                    time + dt)
  //    amr_iteration : where we are in the current AMR subcycle.  Each
  //                    level will take a number of steps to reach the
  //                    final time of the coarser level below it.  This
  //                    counter starts at 1
  //    amr_ncycle    : the number of subcycles at this level

{
    BL_PROFILE("Castro::advance()");

    Real dt_new = dt;

    initialize_advance(time, dt, amr_iteration, amr_ncycle);

    // Do the advance.

    // no SDC

    if (do_ctu) {

      // CTU method is just a single update
      int sub_iteration = 0;
      int sub_ncycle = 0;

      dt_new = do_advance(time, dt, amr_iteration, amr_ncycle, 
			  sub_iteration, sub_ncycle);
    } else {
      for (int iter = 0; iter < MOL_STAGES; ++iter) {
	dt_new = do_advance(time, dt, amr_iteration, amr_ncycle, 
			    iter, MOL_STAGES);
      }
    }

    // Check to see if this advance violated certain stability criteria.
    // If so, get a new timestep and do subcycled advances until we reach
    // t = time + dt.

    if (use_retry)
        dt_new = std::min(dt_new, retry_advance(time, dt, amr_iteration, amr_ncycle));
    if (use_post_step_regrid)
	check_for_post_regrid(time + dt);

#ifdef POINTMASS
    // Update the point mass.
    pointmass_update(time, dt);
#endif

    finalize_advance(time, dt, amr_iteration, amr_ncycle);

    return dt_new;
}

Real
Castro::do_advance (Real time,
                    Real dt,
                    int  amr_iteration,
                    int  amr_ncycle,
                    int  sub_iteration,
                    int  sub_ncycle)
{

  // this routine will advance the old state data (called S_old here)
  // to the new time, for a single level.  The new data is called
  // S_new here.

    BL_PROFILE("Castro::do_advance()");

    const Real prev_time = state[State_Type].prevTime();
    const Real  cur_time = state[State_Type].curTime();

    MultiFab& S_old = get_old_data(State_Type);
    MultiFab& S_new = get_new_data(State_Type);

    // Perform initialization steps.

    initialize_do_advance(time, dt, amr_iteration, amr_ncycle, 
			  sub_iteration, sub_ncycle);

    // Check for NaN's.

    check_for_nan(S_old);

    // Since we are Strang splitting the reactions, do them now (only
    // for first stage of MOL)

    if (do_ctu || (!do_ctu && sub_iteration == 0)) {

      // Initialize the new-time data. This copy needs to come after the
      // reactions.

      MultiFab::Copy(S_new, Sborder, 0, 0, NUM_STATE, S_new.nGrow());

      if (!do_ctu) {
	// store the result of the burn in Sburn for later stages
	MultiFab::Copy(Sburn, Sborder, 0, 0, NUM_STATE, 0);
      }
    }


    // Do the hydro update.  We build directly off of Sborder, which
    // is the state that has already seen the burn 

    if (do_hydro)
    {
      if (do_ctu) {
        //construct_hydro_source(time, dt);
	//apply_source_to_state(S_new, hydro_source, dt);      
      } else {
        construct_mol_hydro_source(time, dt, sub_iteration, sub_ncycle);
      }
    }

    // For MOL integration, we are done with this stage, unless it is
    // the last stage
    if (do_ctu) {

      // Sync up state after old sources and hydro source.

      frac_change = clean_state(S_new, Sborder);

      // Check for NaN's.

      check_for_nan(S_new);

    }

    if (!do_ctu && sub_iteration == sub_ncycle-1) {
      // we just finished the last stage of the MOL integration.
      // Construct S_new now using the weighted sum of the k_mol
      // updates
      

      // compute the hydro update
      hydro_source.setVal(0.0);
      for (int n = 0; n < MOL_STAGES; ++n) {
	MultiFab::Saxpy(hydro_source, dt*b_mol[n], *k_mol[n], 0, 0, hydro_source.nComp(), 0);
      }

      // Apply the update -- we need to build on Sburn, so
      // start with that state
      MultiFab::Copy(S_new, Sburn, 0, 0, S_new.nComp(), 0);
      MultiFab::Add(S_new, hydro_source, 0, 0, S_new.nComp(), 0);
      
      // define the temperature now
      clean_state(S_new);

    }

    if (do_ctu || sub_iteration == sub_ncycle-1) {
      // last part of reactions for CTU and if we are done with the
      // MOL stages

      }

    finalize_do_advance(time, dt, amr_iteration, amr_ncycle, sub_iteration, sub_ncycle);

    return dt;

}



void
Castro::initialize_do_advance(Real time, Real dt, int amr_iteration, int amr_ncycle, 
			      int sub_iteration, int sub_ncycle)
{

    // Reset the change from density resets

    frac_change = 1.e0;

    int finest_level = parent->finestLevel();

    // Reset the grid loss tracking.

    if (track_grid_losses)
      for (int i = 0; i < n_lost; i++)
	material_lost_through_boundary_temp[i] = 0.0;

    // For the hydrodynamics update we need to have NUM_GROW ghost zones available,
    // but the state data does not carry ghost zones. So we use a FillPatch
    // using the state data to give us Sborder, which does have ghost zones.

    if (do_ctu) {
      // for the CTU unsplit method, we always start with the old state
      Sborder.define(grids, dmap, NUM_STATE, NUM_GROW);
      const Real prev_time = state[State_Type].prevTime();
      expand_state(Sborder, prev_time, NUM_GROW);

    } else {
      // for Method of lines, our initialization of Sborder depends on
      // which stage in the RK update we are working on
      
      if (sub_iteration == 0) {

	// first MOL stage
	Sborder.define(grids, dmap, NUM_STATE, NUM_GROW);
	const Real prev_time = state[State_Type].prevTime();
	expand_state(Sborder, prev_time, NUM_GROW);

      } else {

	// the initial state for the kth stage follows the Butcher
	// tableau.  We need to create the proper state starting with
	// the result after the first dt/2 burn (which we copied into
	// Sburn) and we need to fill ghost cells.  

	// We'll overwrite S_old with this information, since we don't
	// need it anymorebuild this state temporarily in S_new (which
	// is State_Data) to allow for ghost filling.
	MultiFab& S_new = get_new_data(State_Type);

	MultiFab::Copy(S_new, Sburn, 0, 0, S_new.nComp(), 0);
	for (int i = 0; i < sub_iteration; ++i)
	  MultiFab::Saxpy(S_new, dt*a_mol[sub_iteration][i], *k_mol[i], 0, 0, S_new.nComp(), 0);

	Sborder.define(grids, dmap, NUM_STATE, NUM_GROW);
	const Real new_time = state[State_Type].curTime();
	expand_state(Sborder, new_time, NUM_GROW);

      }
    }
}



void
Castro::finalize_do_advance(Real time, Real dt, int amr_iteration, int amr_ncycle, int sub_iteration, int sub_ncycle)
{

    Sborder.clear();

}



void
Castro::initialize_advance(Real time, Real dt, int amr_iteration, int amr_ncycle)
{
    // Pass some information about the state of the simulation to a Fortran module.

    ca_set_amr_info(level, amr_iteration, amr_ncycle, time, dt);

    // Save the current iteration.

    iteration = amr_iteration;

    // If the level below this just triggered a special regrid,
    // the coarse contribution to this level's FluxRegister
    // is no longer valid because the grids have, in general, changed.
    // Zero it out, and add them back using the saved copy of the fluxes.

    if (use_post_step_regrid && level > 0)
	if (getLevel(level-1).post_step_regrid)
	    getLevel(level-1).FluxRegCrseInit();

    // The option of whether to do a multilevel initialization is
    // controlled within the radiation class.  This step belongs
    // before the swap.

    // Swap the new data from the last timestep into the old state data.

    for (int k = 0; k < num_state_type; k++) {

	// The following is a hack to make sure that we only
	// ever have new data for a few state types that only
	// ever need new time data; by doing a swap now, we'll
	// guarantee that allocOldData() does nothing. We do
	// this because we never need the old data, so we
	// don't want to allocate memory for it.

	state[k].allocOldData();
	state[k].swapTimeLevels(dt);

    }

    // Ensure data is valid before beginning advance. This addresses
    // the fact that we may have new data on this level that was interpolated
    // from a coarser level, and the interpolation in general cannot be
    // trusted to respect the consistency between certain state variables
    // (e.g. UEINT and UEDEN) that we demand in every zone.

    clean_state(get_old_data(State_Type));

    // Make a copy of the MultiFabs in the old and new state data in case we may do a retry.
    
    if (use_retry) {

      // Store the old and new time levels.

      for (int k = 0; k < num_state_type; k++) {

	prev_state[k].reset(new StateData());

	StateData::Initialize(*prev_state[k], state[k]);

      }

    }

    MultiFab& S_new = get_new_data(State_Type);

    // This array holds the hydrodynamics update.

    hydro_source.define(grids,dmap,NUM_STATE,0);

    if (!do_ctu) {
      // if we are not doing CTU advection, then we are doing a method
      // of lines, and need storage for hte intermediate stages
      k_mol.resize(MOL_STAGES);
      for (int n = 0; n < MOL_STAGES; ++n) {
	k_mol[n].reset(new MultiFab(grids, dmap, NUM_STATE, 0));
	k_mol[n]->setVal(0.0);
      }

      // for the post-burn state
      Sburn.define(grids, dmap, NUM_STATE, 0);
    }

    // Zero out the current fluxes.

    for (int dir = 0; dir < 3; ++dir)
	fluxes[dir]->setVal(0.0);

#if (BL_SPACEDIM <= 2)
    if (!Geometry::IsCartesian())
	P_radial.setVal(0.0);
#endif

}



void
Castro::finalize_advance(Real time, Real dt, int amr_iteration, int amr_ncycle)
{

    // Add the material lost in this timestep to the cumulative losses.

    if (track_grid_losses) {

      ParallelDescriptor::ReduceRealSum(material_lost_through_boundary_temp, n_lost);

      for (int i = 0; i < n_lost; i++)
	material_lost_through_boundary_cumulative[i] += material_lost_through_boundary_temp[i];

    }

    if (do_reflux) {
	FluxRegCrseInit();
	FluxRegFineAdd();
    }

    Real cur_time = state[State_Type].curTime();
    set_special_tagging_flag(cur_time);

    hydro_source.clear();

    amrex::FillNull(prev_state);

    if (!do_ctu) {
      k_mol.clear();
      Sburn.clear();
    }

}



Real
Castro::retry_advance(Real time, Real dt, int amr_iteration, int amr_ncycle)
{

    Real dt_new = 1.e200;
    Real dt_subcycle = 1.e200;

    MultiFab& S_old = get_old_data(State_Type);
    MultiFab& S_new = get_new_data(State_Type);

    const Real* dx = geom.CellSize();

#ifdef _OPENMP
#pragma omp parallel reduction(min:dt_subcycle)
#endif
    for (MFIter mfi(S_new, true); mfi.isValid(); ++mfi) {

        const Box& bx = mfi.tilebox();

	const int* lo = bx.loVect();
	const int* hi = bx.hiVect();

	ca_check_timestep(BL_TO_FORTRAN_3D(S_old[mfi]),
			  BL_TO_FORTRAN_3D(S_new[mfi]),
			  ARLIM_3D(lo), ARLIM_3D(hi), ZFILL(dx),
			  &dt, &dt_subcycle);

    }

    if (retry_neg_dens_factor > 0.0) {

        // Negative density criterion
	// Reset so that the desired maximum fractional change in density
	// is not larger than retry_neg_dens_factor.

        ParallelDescriptor::ReduceRealMin(frac_change);

	if (frac_change < 0.0)
	  dt_subcycle = std::min(dt_subcycle, dt * -(retry_neg_dens_factor / frac_change));

    }

    ParallelDescriptor::ReduceRealMin(dt_subcycle);

    if (dt_subcycle < dt) {

	// Do a basic sanity check to make sure we're not about to overflow.

        if (dt_subcycle * INT_MAX < dt) {
	  if (ParallelDescriptor::IOProcessor()) {
	    std::cout << std::endl;
	    std::cout << "  Timestep " << dt << " rejected at level " << level << "." << std::endl;
	    std::cout << "  The retry mechanism requested subcycled timesteps of maximum length dt = " << dt_subcycle << "," << std::endl
                      << "  but this would imply a number of timesteps that overflows an integer." << std::endl;
	    std::cout << "  The code will abort. Consider decreasing the CFL parameter, castro.cfl," << std::endl
                      << "  to avoid unstable timesteps." << std::endl;
	  }
	  amrex::Abort("Error: integer overflow in retry.");
	}

        int sub_ncycle = ceil(dt / dt_subcycle);

	// Abort if we would take more subcycled timesteps than the user has permitted.

	if (retry_max_subcycles > 0 && sub_ncycle > retry_max_subcycles) {
	  if (ParallelDescriptor::IOProcessor()) {
	    std::cout << std::endl;
	    std::cout << "  Timestep " << dt << " rejected at level " << level << "." << std::endl;
	    std::cout << "  The retry mechanism requested " << sub_ncycle << " subcycled timesteps of maximum length dt = " << dt_subcycle << "," << std::endl
                      << "  but this is more than the maximum number of permitted retry substeps, " << retry_max_subcycles << "." << std::endl;
	    std::cout << "  The code will abort. Consider decreasing the CFL parameter, castro.cfl," << std::endl
                      << "  to avoid unstable timesteps, or consider increasing the parameter " << std::endl 
                      << "  castro.retry_max_subcycles to permit more subcycled timesteps." << std::endl;
	  }
	  amrex::Abort("Error: too many retry timesteps.");
	}

	// Abort if our subcycled timestep would be shorter than the minimum permitted timestep.

	if (dt_subcycle < dt_cutoff) {
	  if (ParallelDescriptor::IOProcessor()) {
	    std::cout << std::endl;
	    std::cout << "  Timestep " << dt << " rejected at level " << level << "." << std::endl;
	    std::cout << "  The retry mechanism requested " << sub_ncycle << " subcycled timesteps of maximum length dt = " << dt_subcycle << "," << std::endl
                      << "  but this timestep is shorter than the user-defined minimum, " << std::endl
                      << "  castro.dt_cutoff = " << dt_cutoff << ". Aborting." << std::endl;
	  }
	  amrex::Abort("Error: retry timesteps too short.");
	}

	if (verbose && ParallelDescriptor::IOProcessor()) {
	  std::cout << std::endl;
	  std::cout << "  Timestep " << dt << " rejected at level " << level << "." << std::endl;
	  std::cout << "  Performing a retry, with " << sub_ncycle
		    << " subcycled timesteps of maximum length dt = " << dt_subcycle << std::endl;
	  std::cout << std::endl;
	}

	Real subcycle_time = time;
	int sub_iteration = 1;
	Real dt_advance = dt / sub_ncycle;

	// Restore the original values of the state data.

	for (int k = 0; k < num_state_type; k++) {

	  if (prev_state[k]->hasOldData())
	      state[k].copyOld(*prev_state[k]);

	  if (prev_state[k]->hasNewData())
	      state[k].copyNew(*prev_state[k]);

	  // Anticipate the swapTimeLevels to come.

	  state[k].swapTimeLevels(0.0);

	  state[k].setTimeLevel(time, 0.0, 0.0);

	}

	if (track_grid_losses)
	  for (int i = 0; i < n_lost; i++)
	    material_lost_through_boundary_temp[i] = 0.0;

	// Subcycle until we've reached the target time.

	while (subcycle_time < time + dt) {

	    // Shorten the last timestep so that we don't overshoot
	    // the ending time. We want to protect against taking
	    // a very small last timestep due to precision issues,
	    // so subtract a small number from that time.

	    Real eps = 1.0e-10 * dt;

	    if (subcycle_time + dt_advance > time + dt - eps)
	        dt_advance = (time + dt) - subcycle_time;

	    if (verbose && ParallelDescriptor::IOProcessor()) {
	        std::cout << "  Beginning retry subcycle " << sub_iteration << " of " << sub_ncycle
		          << ", starting at time " << subcycle_time
		         << " with dt = " << dt_advance << std::endl << std::endl;
	    }

	    for (int k = 0; k < num_state_type; k++) {

		state[k].swapTimeLevels(dt_advance);

	    }

	    do_advance(subcycle_time,dt_advance,amr_iteration,amr_ncycle,sub_iteration,sub_ncycle);

	    if (verbose && ParallelDescriptor::IOProcessor()) {
	        std::cout << std::endl;
	        std::cout << "  Retry subcycle " << sub_iteration << " of " << sub_ncycle << " completed" << std::endl;
	        std::cout << std::endl;
	    }

	  subcycle_time += dt_advance;
	  sub_iteration += 1;

	}

	// We want to return this subcycled timestep as a suggestion,
	// if it is smaller than what the hydro estimates.

	dt_new = std::min(dt_new, dt_subcycle);

	if (verbose && ParallelDescriptor::IOProcessor())
            std::cout << "  Retry subcycling complete" << std::endl << std::endl;

	// Finally, copy the original data back to the old state
	// data so that externally it appears like we took only
	// a single timestep.

	for (int k = 0; k < num_state_type; k++) {

           if (prev_state[k]->hasOldData())
	      state[k].copyOld(*prev_state[k]);

	   state[k].setTimeLevel(time + dt, dt, 0.0);

	}

    }

    return dt_new;

}

