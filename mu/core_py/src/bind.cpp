/**
 * bind.cpp — nanobind module exposing the mu C++ engine to Python.
 *
 * This is a thin binding layer.  All input validation lives in the
 * Python wrapper package (mu.*); the C++ asserts remain as a
 * debug safety-net only.
 *
 * Module name: _mu_core
 */

#include <nanobind/nanobind.h>
#include <nanobind/stl/array.h>
#include <nanobind/ndarray.h>

#include <random>
#include <mu.h>


NB_MODULE(_mu_core, m) {

    m.doc() = "mu card game engine — C++ backend";

}

