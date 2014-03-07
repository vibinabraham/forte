#include "explorer.h"

#include <cmath>

#include <boost/timer.hpp>
#include <boost/format.hpp>

#include "explorer.h"
#include "cartographer.h"
#include "string_determinant.h"

using namespace std;
using namespace psi;

namespace psi{ namespace libadaptive{

double compute_mp_energy(bool *begin, bool *end, const std::vector<double>& epsilon)
{
    double sum = 0.0;
    int n = 0;
    for (bool* it = begin; it != end; ++it) {
        if(*it) sum += epsilon[n];
        ++n;
    }
    return sum;
}

/**
 * Examine all the Slater determinant in the FCI space
 */
void Explorer::examine_all(psi::Options& options)
{
    fprintf(outfile,"\n\n  Exploring the space of Slater determinants\n");
    StringDeterminant det(reference_determinant_);

    // No explorer will succeed without a cartographer
    Cartographer cg(options,min_energy_,min_energy_ + determinant_threshold_);

    int nfrzc = frzcpi_.sum();
    int nfrzv = frzvpi_.sum();
    int naocc = nalpha_ - nfrzc;
    int nbocc = nbeta_ - nfrzc;
    int navir = nmo_ - naocc - nfrzc - nfrzv;
    int nbvir = nmo_ - nbocc - nfrzc - nfrzv;

    // Calculate the maximum excitation level
    maxnaex_ = std::min(naocc,navir);
    maxnbex_ = std::min(nbocc,nbvir);
    minnex_ = options.get_int("MIN_EXC_LEVEL");
    maxnex_ = maxnaex_ + maxnbex_;
    if (options["MAX_EXC_LEVEL"].has_changed()){
        maxnex_ = options.get_int("MAX_EXC_LEVEL");
        maxnaex_ = std::min(maxnex_,maxnaex_);
        maxnbex_ = std::min(maxnex_,maxnbex_);
    }

    boost::timer t;
    double time_string = 0.0;
    double time_dets = 0.0;
    long num_dets_visited = 0;
    long num_dets_accepted = 0;
    unsigned long long num_total_dets = 0;
    unsigned long num_permutations = 0;

    // Allocate an array of bits for fast manipulation
    bool* Ia = new bool[2 * nmo_];
    bool* Ib = &Ia[nmo_];

    // Create the alpha and beta strings |0000011111|
    for (int p = 0; p < nmo_; ++p) Ia[p] = Ib[p] = false;
    for (int i = 0; i < naocc + nfrzc; ++i) Ia[nmo_ - i - 1] = true;
    for (int i = 0; i < nbocc + nfrzc; ++i) Ib[nmo_ - i - 1] = true;

    std::vector<double> epsilon;
    for (int p = 0; p < nmo_; ++p){
        epsilon.push_back(epsilon_a_->get(p));
    }

    // Generate the alpha strings
    bool* Ia_ref = det.get_alfa_bits();
    double a_mp_energy_ref = compute_mp_energy(Ia_ref,Ia_ref + nmo_,epsilon);
    std::vector<std::vector<bool> > astr_vec;
    std::vector<double> ea_vec;
    do{
        double ea = compute_mp_energy(Ia,Ia + nmo_,epsilon) - a_mp_energy_ref;
        if (ea < denominator_threshold_){
            std::vector<bool> bits(Ia,Ia + nmo_);
            astr_vec.push_back(bits);
            ea_vec.push_back(ea);
        }
    } while (std::next_permutation(Ia,Ia + nmo_));

    fprintf(outfile,"\n Number of alpha strings accepted: %d",int(ea_vec.size()));

    // Generate the beta strings
    bool* Ib_ref = det.get_beta_bits();
    double b_mp_energy_ref = compute_mp_energy(Ib_ref,Ib_ref + nmo_,epsilon);
    std::vector<std::vector<bool> > bstr_vec;
    std::vector<double> eb_vec;
    do{
        double eb = compute_mp_energy(Ib,Ib + nmo_,epsilon) - b_mp_energy_ref;
        if (eb < denominator_threshold_){
            std::vector<bool> bits(Ib,Ib + nmo_);
            bstr_vec.push_back(bits);
            eb_vec.push_back(eb);
        }
    } while (std::next_permutation(Ib,Ib + nmo_));

    size_t nastr = astr_vec.size();
    size_t nbstr = bstr_vec.size();
    num_total_dets = nastr * nbstr;
    for (size_t nsa = 0; nsa < nastr; ++nsa){
        double ea = ea_vec[nsa];
        std::vector<bool>& str_sa = astr_vec[nsa];
        for (int p = 0; p < nmo_; ++p) Ia[p] = str_sa[p];
        int ha = string_symmetry(Ia);
        for (size_t nsb = 0; nsb < nbstr; ++nsb){
            double eb = ea_vec[nsb];
            if (ea + eb < denominator_threshold_){
                std::vector<bool>& str_sb = bstr_vec[nsb];
                for (int p = 0; p < nmo_; ++p) Ib[p] = str_sb[p];
                int hb = string_symmetry(Ib);
                if ((ha ^ hb) == wavefunction_symmetry_){
                    det.set_bits(Ia,Ib);
                    double det_energy = det.energy() + nuclear_repulsion_energy_;
                    // check to see if the energy is below a given threshold
                    if (det_energy < min_energy_ + determinant_threshold_){
                        cg.accumulate_data(nmo_,str_sa,str_sb,det_energy,0,0,0,0);
                        if (det_energy < min_energy_){
                            reference_determinant_ = det;
                            min_energy_ = det_energy;
                        }
                        num_dets_accepted++;
                    }
                    num_dets_visited++;
                }
            }
        }
    }

    delete[] Ia;

    fprintf(outfile,"\n\n  The new reference determinant is:");
    reference_determinant_.print();
    fprintf(outfile,"\n  and its energy: %.12f Eh",min_energy_);

    fprintf(outfile,"\n\n  Number of full ci determinants    = %llu",num_total_dets);
    fprintf(outfile,"\n\n  Number of determinants visited    = %ld (%e)",num_dets_visited,double(num_dets_visited) / double(num_total_dets));
    fprintf(outfile,"\n  Number of determinants accepted   = %ld (%e)",num_dets_accepted,double(num_dets_accepted) / double(num_total_dets));
    fflush(outfile);
}

}} // EndNamespaces


