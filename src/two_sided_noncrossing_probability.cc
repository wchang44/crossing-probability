#include <algorithm>
#include <cmath>
#include <cassert>
#include <iostream>
#include <stdexcept>
#include <numeric>
#include <algorithm>
#include "fftwconvolver.hh"

using namespace std;

enum BoundType {H_STEP, G_STEP, END};

struct Bound {
    double location;
    BoundType tag;
};

static bool operator<(Bound b0, Bound b1)
{
    return (b0.location < b1.location);
}

static vector<Bound> join_all_bounds(const vector<double>& h_steps, const vector<double>& g_steps)
{
    assert(h_steps.size() >= g_steps.size());

    vector<Bound> bounds;
    Bound b;

    for (int i = 0; i < (int)h_steps.size(); ++i) {
        b.location = h_steps[i];
        b.tag = H_STEP;
        bounds.push_back(b);
    }

    for (int i = 0; i < (int)g_steps.size(); ++i) {
        b.location = g_steps[i];
        b.tag = G_STEP;
        bounds.push_back(b);
    }

    sort(bounds.begin(), bounds.end());

    b.location = 1.0;
    b.tag = END;
    bounds.push_back(b);

    return bounds;
}

// Computes the probability of a Poisson random variable with intensity lambda:
// Pr[Pois(lambda)=k] = e^-lambda * lambda^k / k!
static inline double poisson_pmf(double lambda, int k)
{
    assert(k >= 0);
    assert(lambda >= 0.0);

    if (lambda == 0.0) {
        return k == 0 ? 1.0 : 0.0;
    }
    double log_pmf = -lambda + k*log(lambda) - lgamma(k+1);
    return exp(log_pmf);
}

// static void print_array(const double* arr, int n)
// {
//     for (int i = 0; i < n; ++i) {
//         cout << arr[i] << ", ";
//     }
//     cout << endl;
// }

static void convolve_same_size(int size, const double* src0, const double* src1, double* dest)
{
    for (int j = 0; j < size; ++j) {
        double convolution_at_j = 0.0;
        for (int k = 0; k <= j; ++k) {
            convolution_at_j += src0[k] * src1[j-k];
        }
        dest[j] = convolution_at_j;
    }
}

static bool lower_and_upper_boundaries_cross(const vector<double>& g_steps, const vector<double>& h_steps)
{
    if (g_steps.size() > h_steps.size()) {
        cout << "The lower and upper boundaries cross: g(1) > h(1).\n";
        return true;
    }
    for (size_t i = 0; i < g_steps.size(); ++i) {
        if (g_steps[i] < h_steps[i]) {
            cout << "The lower and upper boundaries cross! i=" << i << ".\n";
            return true;
        }
    }
    return false;
}

double poisson_process_noncrossing_probability(double intensity, const vector<double>& g_steps, const vector<double>& h_steps, bool use_fft, int endpoint)
{
    if (lower_and_upper_boundaries_cross(g_steps, h_steps)) {
        return 0.0;
    }

    // cout << "intensity: " << intensity << endl;
    // cout << "Computing..." << endl;
    assert((endpoint == -1) || ((endpoint >= (int)g_steps.size()) && (endpoint <= (int)h_steps.size())));
    vector<Bound> bounds = join_all_bounds(h_steps, g_steps);
    // cout << "Total boundary step count: " << bounds.size() << endl;

    int n = h_steps.size();
    vector<double> Qs0(n+1, -1);
    vector<double> Qs1(n+1, -1);
    Qs0[0] = 1.0;

    vector<double>* buffers[] = {&Qs0, &Qs1};

    double prev_location = 0.0;
    int h_step_count = 0;
    int g_step_count = 0;

    FFTWConvolver fftconvolver(n+1);
    vector<double> pmf(n+1, 0.0);
    for (unsigned int i = 0; i < bounds.size(); ++i) {

        const vector<double>& src_buffer = *buffers[i % 2];
        vector<double>& dest_buffer = *buffers[(i+1) % 2];

        double location = bounds[i].location;

        int cur_size = h_step_count - g_step_count + 1;
        for (int j = 0; j < cur_size; ++j) {
            pmf[j] = poisson_pmf(intensity*(location-prev_location), j);
        }

        // cout << "--------------------------------------------\n";
        // cout << "Iteration " << i << endl;
        // cout << "h_step_count: " << h_step_count << endl;
        // cout << "g_step_count: " << g_step_count << endl;
        // cout << "location: " << location << endl;
        // cout << "Src: ";
        // print_array(&src_buffer[0], n+1);
        // cout << "pmf: ";
        // print_array(&pmf[0], n+1);
        // cout << "cur_size: " << cur_size << endl;
        // cout << "dest_buffer before convolution: ";
        // print_array(&dest_buffer[0], n+1);
        
        if (use_fft) {
            fftconvolver.convolve_same_size(cur_size, &pmf[0], &src_buffer[g_step_count], &dest_buffer[g_step_count]);
        } else {
            convolve_same_size(cur_size, &pmf[0], &src_buffer[g_step_count], &dest_buffer[g_step_count]);
        }

        //cout << "dest_buffer after convolution: ";
        //print_array(&dest_buffer[0], n+1);

        BoundType tag = bounds[i].tag;
        if (tag == H_STEP) {
            ++h_step_count;
            Qs0[h_step_count] = 0.0;
            Qs1[h_step_count] = 0.0;
            // cout << "h++\n";
        } else if (tag == G_STEP) {
            Qs0[g_step_count] = 0.0;
            Qs1[g_step_count] = 0.0;
            ++g_step_count;
            // cout << "g++\n";
        } else {
            assert(tag == END);
        }
        prev_location = location;
        //cout << "Dest:";
        //print_array(&dest_buffer[0], n+1);
        //cout << endl;
    }

    vector<double>& last_dest_buffer = *buffers[bounds.size() % 2];
    double nocross_prob;
    if (endpoint == -1) {
        nocross_prob = accumulate(&last_dest_buffer[g_step_count], &last_dest_buffer[h_step_count+1], 0.0);
    } else {
        nocross_prob = last_dest_buffer[endpoint];
    }
    // cout << "Poisson noncrossing probability: " << nocross_prob << endl;
    return nocross_prob;
}

double binomial_process_noncrossing_probability(int n, const vector<double>& g_steps, const vector<double>& h_steps, bool use_fft)
{
    if ((int)g_steps.size() > n) {
        cout << "Binomial process b(t) must cross lower boundary g(t) since g(1) > n and b(t) = n." << endl;
        return 0.0;
    }
    vector<double> processed_h_steps(n, 0.0);
    if (h_steps.size() == 0) {
        // Special case, only the lower bound is specified.
        // We treat this as an implicit upper bound satisfying h(t) = n for all t.
    } else {
        if (lower_and_upper_boundaries_cross(g_steps, h_steps)) {
            return 0.0;
        }
        if ((int)h_steps.size() < n) {
            cout << "Binomial process b(t) must cross upper boundary h(t) since h(1) < n and b(t) = n." << endl;
            return 0.0;
        }
        copy(h_steps.begin(), h_steps.begin() + n, processed_h_steps.begin());
    }
    double poisson_nocross_prob = poisson_process_noncrossing_probability(n, g_steps, processed_h_steps, use_fft, n);
    double binomial_nocross_prob = poisson_nocross_prob / poisson_pmf(n, n);
    // cout << "Binomial noncrossing probability: " << binomial_nocross_prob << endl;
    return binomial_nocross_prob;
}


