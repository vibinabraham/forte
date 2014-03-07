#include "explorer.h"

#include <cmath>

#include <libpsio/psio.hpp>
#include <libmints/wavefunction.h>
#include <libmints/molecule.h>
#include <libmints/pointgrp.h>

#include <boost/timer.hpp>
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>

#include "explorer.h"
#include "string_determinant.h"

using namespace std;
using namespace psi;

namespace psi{ namespace libadaptive{

bool not_bool(bool b)
{
    return (b ? false : true);
}

unsigned long long
choose(unsigned long long n, unsigned long long k) {
    if (k > n) {
        return 0;
    }
    unsigned long long r = 1;
    for (unsigned long long d = 1; d <= k; ++d) {
        r *= n--;
        r /= d;
    }
    return r;
}

bool compare_screened_string(const string_info& t1, const string_info& t2)
{
    if (t1.get<0>() != t2.get<0>()){
        return (t1.get<0>() < t2.get<0>());
    }
    else if (t1.get<1>() != t2.get<1>()){
        return (t1.get<1>() < t2.get<1>());
    }
    return (t1.get<2>() < t2.get<2>());
}

/**
 * @brief Compute alpha or beta strings and screen them using the energy of a determinant
 * @param epsilon - the orbital energies
 * @param nocc - the number of occupied orbitals
 * @param maxnex - the maximum excitation level
 * @return a list of strings organized by irrep and sorted in increasing energetic order
 */
string_list_symm Explorer::compute_strings_screened(vector<double>& epsilon,int nocc,int nvir,int maxnex,bool alpha)
{
    int nact = nocc + nvir;
    bool* I = new bool[nmo_];
    bool* Ia = new bool[nmo_];
    bool* Ib = new bool[nmo_];
    // copy the reference determinant
    StringDeterminant det(reference_determinant_);
    for (int p = 0; p < nmo_; ++p){
        I[p] = false;
        Ia[p] = det.get_alfa_bits()[p];
        Ib[p] = det.get_beta_bits()[p];
    }
    // Set the frozen core orbitals
    for (int i = 0; i < static_cast<int>(frzc_.size()); ++i){
        I[frzc_[i]] = true;
    }

    bool* Im;

    // Choose which string to change
    if(alpha){
        Im = Ia;
    }else{
        Im = Ib;
    }
    string_list_symm vec_str;
    for (int nex = 0; nex <= maxnex; ++nex){
        for(int h = 0; h < nirrep_; ++h){
            string_list empty_sl;
            vec_str.push_back(empty_sl);
        }
    }

    std::vector<double> occ_weights;
    for(int i = 0; i < nocc; ++i){
        occ_weights.push_back(-epsilon[nocc-i-1]);
    }
    std::vector<double> vir_weights;
    for(int a = 0; a < nvir; ++a){
        vir_weights.push_back(epsilon[nocc + a]);
    }

    fprintf(outfile,"\n  Maximum excitation level = %d",maxnex);
    // Loop over excitation level
    for (int nex = 0; nex <= maxnex; ++nex){
        fprintf(outfile,"\n  Excitation level %2d.",nex); fflush(outfile);
        half_string_list vec_occ_str = compute_half_strings_screened(true,nocc,nex,occ_weights,"occupied");
        size_t nso = vec_occ_str.size();
        fprintf(outfile," occupied: %ld",nso); fflush(outfile);

        if (nso == 0){
            fprintf(outfile,"\n  Zero strings at excitation level %d, stopping here.",nex); fflush(outfile);
            break;
        }

        half_string_list vec_vir_str = compute_half_strings_screened(false,nvir,nex,vir_weights,"virtual");
        size_t nsv = vec_vir_str.size();
        fprintf(outfile,", virtual: %ld",nsv); fflush(outfile);
        fprintf(outfile,", total: %ld.",nso * nsv); fflush(outfile);

        if (nso * nsv == 0){
            fprintf(outfile,"\n  Zero strings at excitation level %d, stopping here.",nex); fflush(outfile);
            break;
        }

        fprintf(outfile," Screening strings..."); fflush(outfile);
        // Loop over the occupied strings
        for (size_t so = 0; so < nso; ++so){
            double eo = vec_occ_str[so].first;

            std::vector<bool>& str_so = vec_occ_str[so].second;
            // Copy the string and translate it to Pitzer ordering
            for (int i = 0; i < nocc; ++i) I[qt_to_pitzer_[i]] = str_so[i];

            // Loop over virtual strings
            for (size_t sv = 0; sv < nsv; ++sv){
                double ev = vec_vir_str[sv].first;
                if (eo + ev < denominator_threshold_){
                    // Copy the string and translate it to Pitzer ordering
                    std::vector<bool>& str_sv = vec_vir_str[sv].second;
                    for (int a = 0; a < nvir; ++a) I[qt_to_pitzer_[nocc + a]] = str_sv[a];

                    vector<bool> bits(I,I + nmo_);
                    int h = string_symmetry(I);

                    // set the alpha/beta strings and compute the energy of this determinant
                    // Copy the string and translate it to Pitzer ordering
                    for (int p = 0; p < nmo_; ++p) Im[p] = I[p];
                    det.set_bits(Ia,Ib);
                    double exc_energy = det.excitation_energy(reference_determinant_);

                    int exc_class = excitation_class(nex,h);

                    if(mp_screening_){
                        vec_str[exc_class].push_back(boost::make_tuple(eo + ev,exc_energy,bits));
                    }else if(exc_energy < determinant_threshold_){
                        vec_str[exc_class].push_back(boost::make_tuple(eo + ev,exc_energy,bits));
                    }
                }else{
                    break;
                }
            }
        }
        fprintf(outfile," done."); fflush(outfile);
        // N.B. The vectors of strings are sorted.  This is critical for the algorithm that generates determinants
    }

    for (int nex = 0; nex <= maxnex; ++nex){
        for(int h = 0; h < nirrep_; ++h){
            int exc_class = excitation_class(nex,h);
            sort(vec_str[exc_class].begin(),vec_str[exc_class].end(),compare_screened_string);
        }
    }

    CharacterTable ct = Process::environment.molecule()->point_group()->char_table();

    // Print some information
    fprintf(outfile,"\n  Strings per excitation level and irrep:\n");
    for (int h = 0; h < nirrep_; ++h) fprintf(outfile,"     %5s",ct.gamma(h).symbol());
    fprintf(outfile,"     %5s","Total");

    for (int nex = 0; nex <= maxnex; ++nex){
        size_t ns_total = 0;
        for (int h = 0; h < nirrep_; ++h){
            int exc_class = excitation_class(nex,h);
            size_t ns = vec_str[exc_class].size();
            ns_total += ns;
        }
        if (ns_total > 0){
            fprintf(outfile,"\n  %2d",nex);
            for (int h = 0; h < nirrep_; ++h){
                int exc_class = excitation_class(nex,h);
                size_t ns = vec_str[exc_class].size();
                fprintf(outfile," %9ld",ns);
            }
            fprintf(outfile," %9ld",ns_total);
        }
    }
    fprintf(outfile,"\n Sum");
    for (int h = 0; h < nirrep_; ++h){
        size_t ns = 0;
        for (int nex = 0; nex <= maxnex; ++nex){
            int exc_class = excitation_class(nex,h);
            ns += vec_str[exc_class].size();
        }
        fprintf(outfile," %9ld",ns);
    }
    fflush(outfile);

    delete[] I;
    delete[] Ia;
    delete[] Ib;
    return vec_str;
}

half_string_list Explorer::compute_half_strings_screened(bool is_occ,int n,int k,vector<double>& weights,string label)
{
    bool print_debug = false;
    if(print_debug) fprintf(outfile,"\n      number of %14s strings: %ld",label.c_str(),(long int)choose(n,k));
    half_string_list vec_str;

    // create the first string
    bool* str = new bool[n];
    if(is_occ){
        make_bitmask(str,n,k,true);
    }else{
        make_bitmask(str,n,k,true);
    }

    // generate the rest of the string using next_bound_lex_combination
    int nstr = 0;
    do{
        if(is_occ){
            std::reverse(str,str+n); // reverse the string
            std::transform(str,str+n,str,not_bool); // flip the bits
        }
        double mp_energy = compute_denominator2(is_occ,str,str+n,weights);
        if (mp_energy < denominator_threshold_){
            vector<bool> bits(str,str+n);
            vec_str.push_back(make_pair(mp_energy,bits));
        }
        if(print_debug){
            fprintf(outfile,"\n          %6d: ",nstr);
            for(int i = 0; i < n; ++i){
                fprintf(outfile,"%1d",str[i]);
            }
            fprintf(outfile," %10.4f",mp_energy);
        }
        if(is_occ){
            std::reverse(str,str+n); // reverse the string
            std::transform(str,str+n,str,not_bool); // flip the bits
        }
        nstr++;
    } while (next_bound_lex_combination(denominator_threshold_,weights,str,str+n));

    size_t nvec_str = vec_str.size();
    if(print_debug) fprintf(outfile,", of these %ld are below the denominator threshold",nvec_str);
    delete[] str;
    sort(vec_str.begin(),vec_str.end());
    return vec_str;
}

int Explorer::string_symmetry_qt(bool* I)
{
    int value = 0;
    for (int p = 0; p < nmo_; ++p){
        if (I[p]) value = value ^ mo_symmetry_qt_[p];
    }
    return value;
}

int Explorer::string_symmetry(bool* I)
{
    int value = 0;
    for (int p = 0; p < nmo_; ++p){
        if (I[p]) value = value ^ mo_symmetry_[p];
    }
    return value;
}


/**
* Compute the next combination of true/false in lexicographic order
*
* This algorith is started with a string in which all ones come before the zeros
* for example, s = 111100000, and it produces the sequence
* 111100000
* 111010000
* ..
* 111000001
* 110110000
* ..
* 000001111
*
* with the constraint that the sum of a[s[i]] < max_sum
*/
bool Explorer::next_bound_lex_combination(double max_sum,const std::vector<double>& a,bool* begin,bool* end)
{
    // empty vector
    if(begin == end)
        return false;

    // vector with one element
    bool* i = begin;
    ++i;
    if (i == end)
        return false;

    i = end;
    --i;
    int n = 0;
    for(;;){
        // find the last movable 1
        bool* j = i;
        --i;
        // Look for the case: ij = 10
        if((*i) and not (*j)){
            *i = false;
            *j = true;

            // check what happens to the energy
            double sum = 0.0;
            bool* last = begin;
            // contribution from current arrangement of all - n bits
            for (bool* i = begin; i != end; ++i){
                if (*i){
                    sum += a[i - begin];
                    last = i;
                }
            }
            // contribution from the n trailing ones
            for (bool* i = last + 1; i != last + n + 1; ++i){
                sum += a[i - begin];
            }
            if(sum < max_sum){
                // if n > 0, fill with ones after the position j
                for(int k = 0; k < n; ++k){
                    *(j + k + 1) = true;
                }
                return true;
            }
        }
        // if j = 1, grab this and delete the bit
        if(*j){
            ++n;
            *j = false;
        }
        // i == begin => 00001111
        if (i==begin){
            return false;
        }
    }
    return false;
}


/**
     * Fills a vector of booleans with 0s and 1s.
     * @brief AnalyzeExcitations::make_bitmask
     * @param vec
     * @param n size of the vector
     * @param num1s number of ones
     * @param ones_first If true the 1s come first in the vector
     */
void Explorer::make_bitmask(bool*& vec,int n,int num1s,bool ones_first)
{
    if(ones_first){
        for(int i = 0; i < num1s; ++i) vec[i] = true; // 1
        for(int i = num1s; i < n; ++i) vec[i] = false; // 0
    }else{
        for(int i = 0; i < n - num1s; ++i) vec[i] = false; // 0
        for(int i = n - num1s; i < n; ++i) vec[i] = true; // 1
    }
}

/**
 * Compute the Moller-Plesset energy of a string
 *
 * For occupied strings it returns the sum of the fock matrix elements corresponding
 * to the zeros, e.g. 111010 -> -fock[3][3] - fock[5][5]
 *
 * For virtual strings it returns the sum of the fock matrix elements corresponding
 * to the ones, e.g. 010010 -> +fock[1][1] + fock[4][4]
 *
 * @param is_occ
 * @param begin
 * @param end
 * @param fock
 * @return
 */
double Explorer::compute_denominator(bool is_occ, bool *begin, bool *end, std::vector<double>& epsilon)
{
    double sum = 0.0;
    if(is_occ){
        int n = 0;
        for (bool* it = begin; it != end; ++it){
            if(not *it) sum -= epsilon[n];
            ++n;
        }
    }else{
        int n = static_cast<int>(epsilon.size()) - 1;
        for (bool* it = end - 1; it != begin - 1; --it) {
            if(*it) sum += epsilon[n];
            --n;
        }
    }
    return sum;
}

/**
 * Compute the Moller-Plesset energy of a string
 *
 * For occupied strings it returns the sum of the fock matrix elements corresponding
 * to the zeros, e.g. 111010 -> -fock[3][3] - fock[5][5]
 *
 * For virtual strings it returns the sum of the fock matrix elements corresponding
 * to the ones, e.g. 010010 -> +fock[1][1] + fock[4][4]
 *
 * @param is_occ
 * @param begin
 * @param end
 * @param fock
 * @return
 */
double Explorer::compute_denominator2(bool is_occ, bool *begin, bool *end, std::vector<double>& epsilon)
{
    double sum = 0.0;
    if(is_occ){
        int n = 0;
        for (bool* it = end - 1; it != begin - 1; --it) {
            if(not *it) sum += epsilon[n];
            ++n;
        }
    }else{
        int n = 0;
        for (bool* it = begin; it != end; ++it) {
            if(*it) sum += epsilon[n];
            ++n;
        }
    }
    return sum;
}


/**
 * @brief excitation_class
 * @param nex - the excitation level
 * @param h - the irrep
 * @return the excitaton class
 */
int Explorer::excitation_class(int nex, int h)
{
    return nex * nirrep_ + h;
}

}} // EndNamespaces