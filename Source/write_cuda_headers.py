#!/usr/bin/env python3

# Search the Fortran source code for subroutines marked as:
#
# AMREX_DEVICE subroutine name(...)
#
# and maintain a list of these.
#
# Then copy the C++ headers for Fortran files (typically *_F.H) into a
# temp directory, modifying any of the marked subroutines to have both a
# device and host signature.

import os
import re
import sys
import argparse
import find_files_vpath as ffv

TEMPLATE = """
__global__ static void cuda_{}
{{
   int blo[3];
   int bhi[3];
   for (int k = lo_3 + blockIdx.z * blockDim.z + threadIdx.z; k <= hi_3; k += blockDim.z * gridDim.z) {{
     blo[2] = k;
     bhi[2] = k;
     for (int j = lo_2 + blockIdx.y * blockDim.y + threadIdx.y; j <= hi_2; j += blockDim.y * gridDim.y) {{
       blo[1] = j;
       bhi[1] = j;
       for (int i = lo_1 + blockIdx.x * blockDim.x + threadIdx.x; i <= hi_1; i += blockDim.x * gridDim.x) {{
         blo[0] = i;
         bhi[0] = i;
         {};
       }}
     }}
   }}
}}
"""

# for finding a function signature that starts with DEVICE_LAUNCHABLE
sig_re = re.compile("(DEVICE_LAUNCHABLE)(\\()(.*)(\\))(;)", re.IGNORECASE|re.DOTALL)

# for finding just the variable definitions in the function signature (between the ())
decls_re = re.compile("(.*?)(\\()(.*)(\\))", re.IGNORECASE|re.DOTALL)

# for finding a fortran function subroutine marked with AMREX_DEVICE
fortran_re = re.compile("(AMREX_DEVICE)(\\s+)(subroutine)(\\s+)((?:[a-z][a-z_]+))",
                        re.IGNORECASE|re.DOTALL)


def find_fortran_targets(fortran_names):
    """read through the Fortran files and look for those marked up with
    AMREX_DEVICE"""

    targets = []

    for f in fortran_names:
        ffile = "/".join([f[1], f[0]])

        print("working on {}".format(ffile))

        # open the Fortran file
        try:
            fin = open(ffile, "r")
        except IOError:
            sys.exit("Cannot open Fortran file {}".format(ffile))

        # loop through the file and look for the target subroutines
        line = fin.readline()
        while line:
            m = fortran_re.search(line)
            if m is not None:
                targets.append(m.group(5).lower())

            line = fin.readline()

    return targets


def doit(outdir, fortran_targets, header_files):

    for h in headers_files:
        hdr = "/".join([h[1], h[0]])

        # open the header file
        try:
            hin = open(hdr, "r")
        except IOError:
            sys.exit("Cannot open header {}".format(hdr))

        # open the CUDA header for output
        head, tail = os.path.split(hdr)
        ofile = os.path.join(outdir, tail)
        try:
            hout = open(ofile, "w")
        except IOError:
            sys.exit("Cannot open output file {}".format(ofile))

        # Now write out the CUDA kernels
        hout.write("\n")
        hout.write("#include <AMReX_ArrayLim.H>\n")
        hout.write("#include <AMReX_BLFort.H>\n")
        hout.write("#include <AMReX_Device.H>\n")
        hout.write("\n")

        hdrmh = hdr.strip(".H")

        # Add an include guard
        hout.write("#ifndef _cuda_" + hdrmh + "_\n")
        hout.write("#define _cuda_" + hdrmh + "_\n\n")

        # Wrap the device declarations in extern "C"
        hout.write("#ifdef AMREX_USE_CUDA\n")
        hout.write("extern \"C\" {\n\n")

        line = hin.readline()
        while line:

            # if the line doesn't have DEVICE_LAUNCHABLE, then skip it.
            # otherwise, we need to capture the function signature
            if "DEVICE_LAUNCHABLE" in line:

                launch_sig = "" + line
                sig_end = False
                while not sig_end:
                    line = hin.readline()
                    launch_sig += line
                    if line.strip().endswith(";"):
                        sig_end = True

                # now get just the actual signature
                m = sig_re.search(launch_sig)
                func_sig = m.group(3)

                # First write out the device signature
                device_sig = "__device__ void {};\n\n".format(func_sig)
                device_sig = device_sig.replace("ARLIM_VAL", "ARLIM_REP")
                hout.write(device_sig)

                # Now write out the global signature. This involves getting
                # rid of the data type definitions and also replacing the
                # lo and hi (which must be in the function definition) with blo and bhi.
                dd = decls_re.search(func_sig)
                vars = []

                has_lo = False
                has_hi = False

                for n, v in enumerate(dd.group(3).split(",")):

                    # we will assume that our function signatures _always_ include
                    # the name of the variable
                    _tmp = v.split()
                    var = _tmp[-1].replace("*","").replace("&","").strip()

                    # Replace AMReX Fortran macros
                    var = var.replace("BL_FORT_FAB_ARG_3D", "BL_FORT_FAB_VAL_3D")

                    if var == "ARLIM_VAL(lo)":
                        var = "blo"
                        has_lo = True

                    if var == "ARLIM_VAL(hi)":
                        var = "bhi"
                        has_hi = True

                    vars.append(var)

                if not has_lo or not has_hi:
                    sys.exit("ERROR: function signature must have variables lo and hi defined.")

                # reassemble the function sig
                all_vars = ", ".join(vars)
                new_call = "{}({})".format(dd.group(1), all_vars)

                hout.write(TEMPLATE.format(func_sig, new_call))
                hout.write("\n")

            else:
                # this wasn't a device header
                hout.write(line)


            line = hin.readline()

        # Close out the extern "C" region
        hout.write("\n}\n")
        hout.write("#endif\n")

        # Close out the include guard
        hout.write("\n")
        hout.write("#endif\n")

        hin.close()
        hout.close()

if __name__ == "__main__":

    parser = argparse.ArgumentParser()

    parser.add_argument("--vpath",
                        help="the VPATH to search for files")
    parser.add_argument("--fortran",
                        help="the names of the fortran files to search")
    parser.add_argument("--headers",
                        help="the names of the header files to convert")
    parser.add_argument("--output_dir",
                        help="where to write the new header files",
                        default="")
    args = parser.parse_args()

    # find the location of the Fortran files
    fortran, _ = ffv.find_files(args.vpath, args.fortran)

    # find the names of the Fortran subroutines that are marked as device
    targets = find_fortran_targets(fortran)
    print("targets = ", targets)

    # find the location of the headers
    headers, _ = ffv.find_files(args.vpath, args.headers)

    # copy the headers to the output directory, replacing the signatures
    # of the target Fortran routines with the CUDA pair
    doit(args.output_dir, targets, headers)