#include "Qudit.h"

#include <cmath>
#include <stdexcept>

namespace Potato {
namespace Quantum {

Qudit::Qudit(int dimension, uint64_t seed)
    : dim(dimension), rng(seed ? seed : std::random_device{}()) {
    if (dimension < 2) {
        throw std::invalid_argument("Qudit: dimension 需 >= 2");
    }
    SetUniform();
}

void Qudit::SetProbabilities(const std::vector<double>& probs) {
    if (static_cast<int>(probs.size()) != dim) {
        throw std::invalid_argument("Qudit: 機率向量長度不符");
    }
    double total = 0.0;
    for (double p : probs) total += p;
    if (total <= 0.0) {
        SetUniform();
        return;
    }
    for (int i = 0; i < dim; ++i) {
        amplitudes[i] = Amplitude{std::sqrt(probs[i] / total), 0.0};
    }
}

void Qudit::SetUniform() {
    amplitudes.assign(dim, Amplitude{1.0 / std::sqrt(static_cast<double>(dim)), 0.0});
}

double Qudit::Probability(int i) const {
    if (i < 0 || i >= dim) {
        throw std::out_of_range("Qudit: index 越界");
    }
    return std::norm(amplitudes[i]);
}

std::vector<double> Qudit::Probabilities() const {
    std::vector<double> out(dim);
    for (int i = 0; i < dim; ++i) out[i] = std::norm(amplitudes[i]);
    return out;
}

int Qudit::Measure() {
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    const double roll = dist(rng);
    double acc = 0.0;
    int outcome = dim - 1;
    for (int i = 0; i < dim; ++i) {
        acc += std::norm(amplitudes[i]);
        if (roll < acc) { outcome = i; break; }
    }
    CollapseTo(outcome);
    return outcome;
}

void Qudit::CollapseTo(int i) {
    if (i < 0 || i >= dim) {
        throw std::out_of_range("Qudit: index 越界");
    }
    for (auto& a : amplitudes) a = Amplitude{0.0, 0.0};
    amplitudes[i] = Amplitude{1.0, 0.0};
}

void Qudit::Diffuse(double rate) {
    if (rate <= 0.0) return;
    if (rate > 1.0) rate = 1.0;
    const double uniform = 1.0 / dim;
    std::vector<double> p = Probabilities();
    for (int i = 0; i < dim; ++i) {
        p[i] += (uniform - p[i]) * rate;
        amplitudes[i] = Amplitude{std::sqrt(p[i]), 0.0};
    }
}

bool Qudit::IsNormalized(double eps) const {
    double total = 0.0;
    for (const auto& a : amplitudes) total += std::norm(a);
    return std::abs(total - 1.0) < eps;
}

} // namespace Quantum
} // namespace Potato
