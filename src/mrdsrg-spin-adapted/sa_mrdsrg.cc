/*
 * @BEGIN LICENSE
 *
 * Forte: an open-source plugin to Psi4 (https://github.com/psi4/psi4)
 * that implements a variety of quantum chemistry methods for strongly
 * correlated electrons.
 *
 * Copyright (c) 2012-2020 by its authors (see COPYING, COPYING.LESSER, AUTHORS).
 *
 * The copyrights for code used from other parties are included in
 * the corresponding files.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see http://www.gnu.org/licenses/.
 *
 * @END LICENSE
 */

#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

#include "boost/format.hpp"

#include "psi4/libpsi4util/PsiOutStream.h"
#include "psi4/libmints/molecule.h"

#include "base_classes/active_space_solver.h"
#include "fci/fci_solver.h"
#include "sci/fci_mo.h"
#include "helpers/printing.h"
#include "orbital-helpers/semi_canonicalize.h"
#include "orbital-helpers/mp2_nos.h"
#include "sa_mrdsrg.h"

using namespace psi;

namespace forte {

SA_MRDSRG::SA_MRDSRG(RDMs rdms, std::shared_ptr<SCFInfo> scf_info,
                     std::shared_ptr<ForteOptions> options, std::shared_ptr<ForteIntegrals> ints,
                     std::shared_ptr<MOSpaceInfo> mo_space_info)
    : SADSRG(rdms, scf_info, options, ints, mo_space_info) {

    print_method_banner({"Nonperturbative MR-DSRG"});

    read_options();
    startup();
    print_options();
}

void SA_MRDSRG::read_options() {
    corrlv_string_ = foptions_->get_str("CORR_LEVEL");
    std::vector<std::string> available{"LDSRG2", "LDSRG2_QC"};
    if (std::find(available.begin(), available.end(), corrlv_string_) == available.end()) {
        outfile->Printf("\n  Warning: CORR_LEVEL option %s is not implemented.",
                        corrlv_string_.c_str());
        outfile->Printf("\n  Changed CORR_LEVEL option to LDSRG2_QC");

        corrlv_string_ = "LDSRG2_QC";
        warnings_.push_back(std::make_tuple("Unsupported CORR_LEVEL", "Change to LDSRG2_QC",
                                            "Change options in input.dat"));
    }

    sequential_Hbar_ = foptions_->get_bool("DSRG_HBAR_SEQ");
    nivo_ = foptions_->get_bool("DSRG_NIVO");
}

void SA_MRDSRG::startup() {
    // prepare integrals
    build_ints();

    // link F_ with Fock_ of SADSRG
    F_ = Fock_;

    // test semi-canonical
    if (!semi_canonical_) {
        outfile->Printf("\n    Orbital invariant formalism will be employed for MR-DSRG.");
        U_ = ambit::BlockedTensor::build(tensor_type_, "U", {"gg"});
        Fdiag_ = diagonalize_Fock_diagblocks(U_);
    }
}

void SA_MRDSRG::print_options() {
    // fill in information
    std::vector<std::pair<std::string, int>> calculation_info{
        {"DIIS start", diis_start_},
        {"Min DIIS vectors", diis_min_vec_},
        {"Max DIIS vectors", diis_max_vec_},
        {"DIIS extrapolating freq", diis_freq_},
        {"Number of amplitudes for printing", ntamp_}};

    std::vector<std::pair<std::string, double>> calculation_info_double{
        {"Flow parameter", s_},
        {"Taylor expansion threshold", pow(10.0, -double(taylor_threshold_))},
        {"Intruder amplitudes threshold", intruder_tamp_}};

    std::vector<std::pair<std::string, std::string>> calculation_info_string{
        {"Correlation level", corrlv_string_},
        {"Integral type", ints_type_},
        {"Source operator", source_},
        {"Reference relaxation", relax_ref_},
        {"Core-Virtual source type", ccvv_source_}};

    auto true_false_string = [](bool x) {
        if (x) {
            return std::string("TRUE");
        } else {
            return std::string("FALSE");
        }
    };
    calculation_info_string.push_back(
        {"Sequential DSRG transformation", true_false_string(sequential_Hbar_)});
    calculation_info_string.push_back(
        {"Omit blocks of >= 3 virtual indices", true_false_string(nivo_)});

    // print some information
    print_h2("Calculation Information");
    for (auto& str_dim : calculation_info) {
        outfile->Printf("\n    %-40s %15d", str_dim.first.c_str(), str_dim.second);
    }
    for (auto& str_dim : calculation_info_double) {
        outfile->Printf("\n    %-40s %15.3e", str_dim.first.c_str(), str_dim.second);
    }
    for (auto& str_dim : calculation_info_string) {
        outfile->Printf("\n    %-40s %15s", str_dim.first.c_str(), str_dim.second.c_str());
    }
    outfile->Printf("\n");
}

void SA_MRDSRG::build_ints() {
    // prepare one-electron integrals
    H_ = BTF_->build(tensor_type_, "H", {"gg"});
    H_.iterate([&](const std::vector<size_t>& i, const std::vector<SpinType>&, double& value) {
        value = ints_->oei_a(i[0], i[1]);
    });

    // prepare two-electron integrals or three-index B
    if (eri_df_) {
        B_ = BTF_->build(tensor_type_, "B 3-idx", {"Lgg"});
        fill_three_index_ints(B_);
    } else {
        V_ = BTF_->build(tensor_type_, "V", {"gggg"});

        for (const std::string& block : V_.block_labels()) {
            auto mo_to_index = BTF_->get_mo_to_index();

            std::vector<size_t> i0 = mo_to_index[block.substr(0, 1)];
            std::vector<size_t> i1 = mo_to_index[block.substr(1, 1)];
            std::vector<size_t> i2 = mo_to_index[block.substr(2, 1)];
            std::vector<size_t> i3 = mo_to_index[block.substr(3, 1)];

            auto Vblock = ints_->aptei_ab_block(i0, i1, i2, i3);
            V_.block(block).copy(Vblock);
        }
    }
}

double SA_MRDSRG::compute_energy() {
    // build initial amplitudes
    print_h2("Build Initial Amplitude from DSRG-MRPT2");
    T1_ = BTF_->build(tensor_type_, "T1 Amplitudes", {"hp"});
    T2_ = BTF_->build(tensor_type_, "T2 Amplitudes", {"hhpp"});

    if (eri_df_) {
        guess_t_df(B_, T2_, F_, T1_);
    } else {
        guess_t(V_, T2_, F_, T1_);
    }

    // check initial amplitudes
    analyze_amplitudes("First-Order", T1_, T2_);

    // get reference energy
    double Etotal = Eref_;

    // compute energy
    Etotal += compute_energy_ldsrg2();
    //    switch (corrlevelmap[corrlv_string_]) {
    //    case CORR_LV::LDSRG2: {
    //        Etotal += compute_energy_ldsrg2();
    //        break;
    //    }
    //    default: { Etotal += compute_energy_ldsrg2_qc(); }
    //    }

    return Etotal;
}

double SA_MRDSRG::Hbar_od_norm(const int& n, const std::vector<std::string>& blocks) {
    double norm = 0.0;

    auto T = (n == 1) ? Hbar1_ : Hbar2_;

    for (auto& block : blocks) {
        double norm_block = T.block(block).norm();
        norm += 2.0 * norm_block * norm_block;
    }
    norm = std::sqrt(norm);

    return norm;
}

} // namespace forte