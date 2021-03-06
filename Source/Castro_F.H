#ifndef _Castro_F_H_
#define _Castro_F_H_
#include <AMReX_BLFort.H>

#ifdef __cplusplus
#include <AMReX.H>
extern "C"
{
#endif

  void network_init();

  void network_finalize();

  void eos_init();

  void eos_finalize();

  CASTRO_DEVICE
  void ctoprim(const int* lo, const int* hi,
               const amrex::Real* u, const int* u_lo, const int* u_hi,
               const amrex::Real* q, const int* q_lo, const int* q_hi,
               const amrex::Real* qaux, const int* qa_lo, const int* qa_hi);

  CASTRO_DEVICE
  void enforce_minimum_density
    (const int* lo, const int* hi, BL_FORT_FAB_ARG_3D(state));

  CASTRO_DEVICE
  void normalize_species
    (const int* lo, const int* hi, BL_FORT_FAB_ARG_3D(state));

  CASTRO_DEVICE
  void reset_internal_e
    (const int* lo, const int* hi, BL_FORT_FAB_ARG_3D(state));

  CASTRO_DEVICE
  void compute_temp
    (const int* lo, const int* hi, BL_FORT_FAB_ARG_3D(state));

  CASTRO_DEVICE
  void estdt
    (const int* lo, const int* hi,
     const BL_FORT_FAB_ARG_3D(state),
     const amrex::Real* dx, amrex::Real* dt);

  CASTRO_DEVICE
  void store_flux
    (const int* lo, const int* hi,
     BL_FORT_FAB_ARG_3D(flux_out),
     BL_FORT_FAB_ARG_3D(flux_in),
     BL_FORT_FAB_ARG_3D(area),
     const amrex::Real dt);

  CASTRO_DEVICE
  void fill_hydro_source
    (const int* lo, const int* hi,
     const BL_FORT_FAB_ARG_3D(state),
     const BL_FORT_FAB_ARG_3D(q),
     BL_FORT_FAB_ARG_3D(source),
     const BL_FORT_FAB_ARG_3D(flux0),
     const BL_FORT_FAB_ARG_3D(flux1),
     const BL_FORT_FAB_ARG_3D(flux2),
     const BL_FORT_FAB_ARG_3D(qe0),
     const BL_FORT_FAB_ARG_3D(qe1),
     const BL_FORT_FAB_ARG_3D(qe2),
     const BL_FORT_FAB_ARG_3D(area0),
     const BL_FORT_FAB_ARG_3D(area1),
     const BL_FORT_FAB_ARG_3D(area2),
     const BL_FORT_FAB_ARG_3D(volume),
     const amrex::Real* dx, const amrex::Real dt);

  CASTRO_DEVICE
  void normalize_species_fluxes
    (const int* lo, const int* hi,
     BL_FORT_FAB_ARG_3D(flux));

  CASTRO_DEVICE
  void apply_av
    (const int* lo, const int* hi,
     const int idir, const amrex::Real* dx,
     BL_FORT_FAB_ARG_3D(div),
     BL_FORT_FAB_ARG_3D(Sborder),
     BL_FORT_FAB_ARG_3D(flux));

  CASTRO_DEVICE
  void trans1(const int* lo, const int* hi,
              const int idir1, const int idir2,
              const BL_FORT_FAB_ARG_3D(q2m),
              BL_FORT_FAB_ARG_3D(q2mo),
              const BL_FORT_FAB_ARG_3D(q2p),
              BL_FORT_FAB_ARG_3D(q2po),
              const BL_FORT_FAB_ARG_3D(qaux),
              const BL_FORT_FAB_ARG_3D(f1),
              const BL_FORT_FAB_ARG_3D(q1),
              const amrex::Real cdtdx);

  CASTRO_DEVICE
  void trans2(const int* lo, const int* hi,
              const int idir1, const int idir2, const int idir3,
              const BL_FORT_FAB_ARG_3D(qm),
              BL_FORT_FAB_ARG_3D(qmo),
              const BL_FORT_FAB_ARG_3D(qp),
              BL_FORT_FAB_ARG_3D(qpo),
              const BL_FORT_FAB_ARG_3D(qaux),
              const BL_FORT_FAB_ARG_3D(fyz),
              const BL_FORT_FAB_ARG_3D(fzy),
              const BL_FORT_FAB_ARG_3D(qy),
              const BL_FORT_FAB_ARG_3D(qz),
              const amrex::Real hdtdx, const amrex::Real cdtdy, const amrex::Real cdtdz);

  CASTRO_DEVICE
  void compute_flux
    (const int* lo, const int* hi,
     const BL_FORT_FAB_ARG_3D(ql),
     const BL_FORT_FAB_ARG_3D(qr),
     BL_FORT_FAB_ARG_3D(flux),
     BL_FORT_FAB_ARG_3D(q_int),
     BL_FORT_FAB_ARG_3D(qe),
     const BL_FORT_FAB_ARG_3D(qaux),
     const int idir);

  CASTRO_DEVICE
  void divu
    (const int* lo, const int* hi,
     BL_FORT_FAB_ARG_3D(q),
     const amrex::Real* dx,
     BL_FORT_FAB_ARG_3D(div));

  CASTRO_DEVICE
  void trace_ppm
     (const int* lo, const int* hi,
      const int* vlo, const int* vhi,
      const int idir,
      const BL_FORT_FAB_ARG_3D(q),
      const BL_FORT_FAB_ARG_3D(qaux),
      BL_FORT_FAB_ARG_3D(qm),
      BL_FORT_FAB_ARG_3D(qp),
      const int* domlo, const int* domhi,
      const amrex::Real* dx, const amrex::Real dt);

  CASTRO_DEVICE
  void initdata
    (const int* lo, const int* hi,
     BL_FORT_FAB_ARG_3D(state),
     const amrex::Real* dx, const amrex::Real* problo, const amrex::Real* probhi);

  CASTRO_DEVICE
  void denerror
    (const int* lo, const int* hi,
     int8_t* tag, const int* tag_lo, const int* tag_hi,
     const amrex::Real* den, const int* den_lo, const int* den_hi,
     const int8_t tagval, const int8_t clearval);

  CASTRO_DEVICE
  void calculate_blast_radius
    (const int* lo, const int* hi,
     BL_FORT_FAB_ARG_3D(state),
     const amrex::Real* dx, const amrex::Real* problo, const amrex::Real* probhi,
     amrex::Real* blast_mass, amrex::Real* blast_radius,
     const amrex::Real max_density);

#ifdef __cplusplus
}
#endif



#endif
