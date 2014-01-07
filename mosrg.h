/*
 *@BEGIN LICENSE
 *
 * Libadaptive: an ab initio quantum chemistry software package
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 *@END LICENSE
 */

#ifndef _mosrg_h_
#define _mosrg_h_

#include <fstream>

#include "mobase.h"
#include "tensor.h"

namespace psi{ namespace libadaptive{

/* The type of container used to hold the state vector  *
 * Used by boost::odeint                                */
typedef std::vector<double> odeint_state_type;

class MOSRG : public MOBase
{
    enum SRGCommutators {SRCommutators,MRNOCommutators,MRCommutators};
    enum SRGOperator  {SRGOpUnitary,SRGOpCC};
public:
    // Constructor and destructor
    MOSRG(Options &options, ExplorerIntegrals* ints, TwoIndex G1aa, TwoIndex G1bb);
    ~MOSRG();
private:
    /// The type of operator used
    SRGOperator srgop;
    /// The type of commutators used in the computations
    SRGCommutators srgcomm;
    /// The scalar component of the similarity-transformed Hamiltonian
    double Hbar0_;
    /// The one-body component of the similarity-transformed Hamiltonian
    MOTwoIndex Hbar1_;
    /// The two-body component of the similarity-transformed Hamiltonian
    MOFourIndex Hbar2_;
    /// The one-body component of the flow generator
    MOTwoIndex eta1_;
    /// The two-body component of the flow generator
    MOFourIndex eta2_;
    /// An intermediate one-body component of the similarity-transformed Hamiltonian
    MOTwoIndex O1_;
    /// An intermediate two-body component of the similarity-transformed Hamiltonian
    MOFourIndex O2_;
    /// An intermediate one-body component of the similarity-transformed Hamiltonian
    MOTwoIndex C1_;
    /// An intermediate two-body component of the similarity-transformed Hamiltonian
    MOFourIndex C2_;
    /// The scalar component of the operator S
    double S0_;
    /// The one-body component of the operator S
    MOTwoIndex S1_;
    /// The one-body component of the operator S
    MOFourIndex S2_;

    Tensor tA_aaaa;
    Tensor tA_abab;
    Tensor tA_bbbb;
    Tensor tAm_aaaa;
    Tensor tAm_abab;
    Tensor tAm_bbbb;
    Tensor tB_aaaa;
    Tensor tB_abab;
    Tensor tB_bbbb;
    Tensor tBm_aaaa;
    Tensor tBm_abab;
    Tensor tBm_bbbb;
    Tensor tC_aaaa;
    Tensor tC_abab;
    Tensor tC_bbbb;

    void mosrg_startup();
    void mosrg_cleanup();

    /// The SRG routines
    void compute_similarity_renormalization_group();
    void compute_similarity_renormalization_group_step();

    void compute_canonical_transformation_energy();
    double compute_recursive_single_commutator();

    void compute_driven_srg_energy();
    /// The contributions to the one-body DSRG equations
    void one_body_driven_srg();
    /// The contributions to the two-body DSRG equations
    void two_body_driven_srg();

    void update_S1();
    void update_S2();

    /// Functions to compute commutators C += factor * [A,B]
    void commutator_A_B_C(double factor,
                          MOTwoIndex restrict A1,MOFourIndex restrict A2,
                          MOTwoIndex restrict B1,MOFourIndex restrict B2,
                          double& C0,MOTwoIndex restrict C1,MOFourIndex restrict C2);
    /// Functions to compute commutators C += factor * [A,B] but the term [A2,B2] -> C1
    /// contains a factor of two to recover the correct prefactor for the fourth-order term
    /// 1/2 [[V,T2],T2] -> R2
    void commutator_A_B_C_fourth_order(double factor,
                          MOTwoIndex restrict A1,MOFourIndex restrict A2,
                          MOTwoIndex restrict B1,MOFourIndex restrict B2,
                          double& C0,MOTwoIndex restrict C1,MOFourIndex restrict C2);
    /// Functions to compute commutators C += factor * [A,B] as done in the SRG(2) approximation
    void commutator_A_B_C_SRG2(double factor,
                          MOTwoIndex restrict A1,MOFourIndex restrict A2,
                          MOTwoIndex restrict B1,MOFourIndex restrict B2,
                          double& C0,MOTwoIndex restrict C1,MOFourIndex restrict C2);
    /// The numbers indicate the rank of each operator
    void commutator_A1_B1_C0(MOTwoIndex restrict A,MOTwoIndex restrict B,double sign,double& C);
    void commutator_A1_B1_C1(MOTwoIndex restrict A,MOTwoIndex restrict B,double sign,MOTwoIndex C);
    void commutator_A1_B2_C0(MOTwoIndex restrict A,MOFourIndex restrict B,double sign,double& C);
    void commutator_A1_B2_C1(MOTwoIndex restrict A,MOFourIndex restrict B,double sign,MOTwoIndex C);
    void commutator_A1_B2_C2(MOTwoIndex restrict A,MOFourIndex restrict B,double sign,MOFourIndex C);
    void commutator_A2_B2_C0(MOFourIndex restrict A,MOFourIndex restrict B,double sign,double& C);
    void commutator_A2_B2_C1(MOFourIndex restrict A,MOFourIndex restrict B,double sign,MOTwoIndex C);
    void commutator_A2_B2_C2(MOFourIndex restrict A,MOFourIndex restrict B,double sign,MOFourIndex C);
    void print_timings();

    friend class MOSRG_ODEInterface;
};

/// This class helps interface the SRG class to the boost ODE integrator
class MOSRG_ODEInterface {
    MOSRG& mosrg_obj_;
    int neval_;
public:
    MOSRG_ODEInterface(MOSRG& mosrg_obj) : mosrg_obj_(mosrg_obj), neval_(0) { }
    void operator() (const odeint_state_type& x,odeint_state_type& dxdt,const double t);
    int neval() {return neval_;}
};

}} // End Namespaces

#endif // _mosrg_h_
