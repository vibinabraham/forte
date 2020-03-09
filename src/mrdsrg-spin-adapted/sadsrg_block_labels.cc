#include <algorithm>

#include "psi4/libpsi4util/process.h"
#include "psi4/libmints/molecule.h"
#include "psi4/libmints/dipole.h"

#include "sadsrg.h"

using namespace psi;

namespace forte {

std::vector<std::string> SADSRG::diag_one_labels() {
    std::vector<std::string> labels;
    for (const std::string& p : {core_label_, actv_label_, virt_label_}) {
        labels.push_back(p + p);
    }
    return labels;
}

std::vector<std::string> SADSRG::od_one_labels_hp() {
    std::vector<std::string> labels;
    for (const std::string& p : {core_label_, actv_label_}) {
        for (const std::string& q : {actv_label_, virt_label_}) {
            if (p == actv_label_ && q == actv_label_) {
                continue;
            }
            labels.push_back(p + q);
        }
    }
    return labels;
}

std::vector<std::string> SADSRG::od_one_labels_ph() {
    std::vector<std::string> blocks1(od_one_labels_hp());
    for (auto& block : blocks1) {
        std::swap(block[0], block[1]);
    }
    return blocks1;
}

std::vector<std::string> SADSRG::od_one_labels() {
    std::vector<std::string> labels(od_one_labels_hp());
    std::vector<std::string> temp(od_one_labels_ph());
    labels.insert(std::end(labels), std::begin(temp), std::end(temp));
    return labels;
}

std::vector<std::string> SADSRG::od_two_labels_hhpp() {
    std::vector<std::string> labels;
    for (const std::string& p : {core_label_, actv_label_}) {
        for (const std::string& q : {core_label_, actv_label_}) {
            for (const std::string& r : {actv_label_, virt_label_}) {
                for (const std::string& s : {actv_label_, virt_label_}) {
                    if (p == actv_label_ && q == actv_label_ && r == actv_label_ &&
                        s == actv_label_) {
                        continue;
                    }
                    labels.push_back(p + q + r + s);
                }
            }
        }
    }
    return labels;
}

std::vector<std::string> SADSRG::od_two_labels_pphh() {
    std::vector<std::string> labels(od_two_labels_hhpp());
    for (auto& block : labels) {
        std::swap(block[0], block[2]);
        std::swap(block[1], block[3]);
    }
    return labels;
}

std::vector<std::string> SADSRG::od_two_labels() {
    std::vector<std::string> labels(od_two_labels_hhpp());
    std::vector<std::string> temp(od_two_labels_pphh());
    labels.insert(std::end(labels), std::begin(temp), std::end(temp));
    return labels;
}

std::vector<std::string> SADSRG::diag_two_labels() {
    std::vector<std::string> general{core_label_, actv_label_, virt_label_};

    std::vector<std::string> all;
    for (const std::string& p : general) {
        for (const std::string& q : general) {
            for (const std::string& r : general) {
                for (const std::string& s : general) {
                    all.push_back(p + q + r + s);
                }
            }
        }
    }

    std::vector<std::string> od(od_two_labels());
    std::sort(od.begin(), od.end());
    std::sort(all.begin(), all.end());

    std::vector<std::string> labels;
    std::set_symmetric_difference(all.begin(), all.end(), od.begin(), od.end(),
                                  std::back_inserter(labels));

    return labels;
}

std::vector<std::string> SADSRG::re_two_labels() {
    std::vector<std::vector<std::string>> half_labels{
        {core_label_ + core_label_},
        {actv_label_ + actv_label_},
        {virt_label_ + virt_label_},
        {core_label_ + actv_label_, actv_label_ + core_label_},
        {core_label_ + virt_label_, virt_label_ + core_label_},
        {actv_label_ + virt_label_, virt_label_ + actv_label_}};

    std::vector<std::string> labels;
    for (const auto& half : half_labels) {
        for (const std::string& half1 : half) {
            for (const std::string& half2 : half) {
                labels.push_back(half1 + half2);
            }
        }
    }

    return labels;
}

} // namespace forte