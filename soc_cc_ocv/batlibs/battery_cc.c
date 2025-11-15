#include "battery_cc.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "battery_soc";

// ----- helper linear algebra (small sizes) -----
// solve (A)x=b by Gaussian elimination, A is n x n, stored row-major.
// This is naive but ok for n <= 64.
static int solve_linear_system(float *A, float *b, float *x, int n)
{
    // augment A with b (in place copy)
    float *M = malloc(sizeof(float)*(n)*(n+1));
    if (!M) return -1;
    for (int r=0;r<n;r++){
        for (int c=0;c<n;c++) M[r*(n+1)+c] = A[r*n + c];
        M[r*(n+1)+n] = b[r];
    }
    // elimination
    for (int i=0;i<n;i++){
        // pivot
        int piv = i;
        float maxv = fabsf(M[i*(n+1)+i]);
        for (int r=i+1;r<n;r++){
            float v = fabsf(M[r*(n+1)+i]);
            if (v > maxv){ maxv = v; piv = r;}
        }
        if (maxv < 1e-9f) { free(M); return -2; } // singular
        if (piv != i){
            for (int c=i;c<=n;c++){
                float tmp = M[i*(n+1)+c];
                M[i*(n+1)+c] = M[piv*(n+1)+c];
                M[piv*(n+1)+c] = tmp;
            }
        }
        // normalize row
        float diag = M[i*(n+1)+i];
        for (int c=i;c<=n;c++) M[i*(n+1)+c] /= diag;
        // eliminate below
        for (int r=i+1;r<n;r++){
            float f = M[r*(n+1)+i];
            if (fabsf(f) < 1e-12f) continue;
            for (int c=i;c<=n;c++) M[r*(n+1)+c] -= f * M[i*(n+1)+c];
        }
    }
    // back substitution
    for (int i=n-1;i>=0;i--){
        float val = M[i*(n+1)+n];
        for (int c=i+1;c<n;c++) val -= M[i*(n+1)+c] * x[c];
        x[i] = val; 
    }
    free(M);
    return 0;
}

// ----- API impl -----
esp_err_t bsoc_init(battery_soc_t *h, float capacity_Ah)
{
    if (!h || capacity_Ah <= 0 ) return ESP_ERR_INVALID_ARG;
    memset(h,0,sizeof(*h));
    h->last_sample_time_us = -1;
    h->last_I = 0.0f;
    h->rated_capacity_Ah = capacity_Ah;
    h->eta_charge = 0.9952f;
    h->eta_discharge = 0.9952f;
    h->soc_cc = 1.0f; // assume full unless loaded
    h->soh = 1.0f;
    h->r_int = 0.05f; // default guess
    return ESP_OK;
}

void bsoc_deinit(battery_soc_t *h)
{
    if (!h) return;
    free(h->v_buf); free(h->i_buf);
    h->v_buf = NULL; h->i_buf = NULL;
}

// set efficiencies (0..1)
void bsoc_set_efficiencies(battery_soc_t *h, float eta_c, float eta_d)
{
    if (!h) return;
    h->eta_charge = fmaxf(0.5f, fminf(1.0f, eta_c));
    h->eta_discharge = fmaxf(0.5f, fminf(1.0f, eta_d));
}

void bsoc_set_rint(battery_soc_t *h, float r_ohm)
{
    if (!h) return;
    h->r_int = r_ohm;
}

// feed one sample (V in V, I in A). Convention: I>0 means discharge (current leaving battery)
void bsoc_feed_sample(battery_soc_t *h, float V_term, float I_A)
{
    if (!h) return;
    int64_t now_us = esp_timer_get_time(); // microseconds
    if (h->last_sample_time_us < 0) {
        // first sample, initialize buffers and timestamps
        h->last_sample_time_us = now_us;
        h->last_I = I_A;
        return;
    }

    double dt_s = (now_us - h->last_sample_time_us) * 1e-6; // seconds
    
    // Trapezoid integration:
    double delta_Q_Ah = 0.5 * (h->last_I + I_A) * dt_s / 3600.0; // Ah
    ESP_LOGI(TAG, "Mah : %lf",delta_Q_Ah);
    // Convert to fraction of capacity
    double delta_frac = (double)delta_Q_Ah / (double)h->rated_capacity_Ah;
    // apply efficiency depending on charge/discharge:
    // Convention in code: I>0 => discharge (reduces SOC)
    if ((h->last_I + I_A)/2.0 < 0.0) {
        // discharging
        delta_frac *= h->eta_discharge;
    } else {
        // charging (average current negative)
        // charging efficiency reduces effective added charge:
        delta_frac *= h->eta_charge; // or multiply by eta_charge depending on sign definition
    }
    // update SOC (soc stored as 0..1, soc_cc decreases when discharging)
    h->soc_cc += (float)delta_frac;
    if (h->soc_cc > 1.0f) h->soc_cc = 1.0f;
    if (h->soc_cc < 0.0f) h->soc_cc = 0.0f;

    // save for next step
    h->last_sample_time_us = now_us;
    h->last_I = I_A;
}

// simple SOC read (in percent)
float bsoc_get_soc_percent(battery_soc_t *h)
{
    if (!h) return 0.0f;
    return h->soc_cc * 100.0f;
}
float bsoc_get_soh_percent(battery_soc_t *h)
{
    if (!h) return 0.0f;
    return h->soh * 100.0f;
}

// Universal deconvolution implementation (simplified):
// Build Toeplitz A (n x m) from i_buf, solve for f that approximates impulse at center.
// We use normal equations with Tikhonov regularization: (A^T A + lambda I) f = A^T e
esp_err_t bsoc_run_universal(battery_soc_t *h, float reg_lambda)
{
    if (!h) return ESP_ERR_INVALID_ARG;
    int n = h->buf_count;
    if (n < 8) return ESP_ERR_INVALID_STATE;
    // we'll set m = n (filter length)
    int m = n;
    // prepare i vector in chronological order (oldest..newest)
    float *ivec = malloc(sizeof(float)*n);
    float *vvec = malloc(sizeof(float)*n);
    if (!ivec || !vvec) { free(ivec); free(vvec); return ESP_ERR_NO_MEM; }
    int start = (h->buf_idx - h->buf_count + h->window_size) % h->window_size;
    for (int k=0;k<n;k++){
        int idx = (start + k) % h->window_size;
        ivec[k] = h->i_buf[idx];
        vvec[k] = h->v_buf[idx];
    }
    // Build A: n x m where A[row,col] = ivec[row - col] if row - col >=0 else 0
    // But we only need A^T * A (m x m) and A^T * e (m)
    float *AtA = calloc(m*m, sizeof(float));
    float *AtE = calloc(m, sizeof(float));
    if (!AtA || !AtE) { free(ivec); free(vvec); free(AtA); free(AtE); return ESP_ERR_NO_MEM; }

    // e vector is delta at last sample (choose last index -> impulse at n-1)
    int epos = n - 1;

    // compute AtA and AtE
    for (int r=0;r<n;r++){
        for (int c=0;c<m;c++){
            float a_rc = 0.0f;
            int idx = r - c;
            if (idx >= 0) a_rc = ivec[idx];
            // accumulate AtA (col,row) -> AtA[col*m + col2] add a_rc * a_rcol2
            for (int col2=0; col2<m; col2++){
                float a_rcol2 = 0.0f;
                int idx2 = r - col2;
                if (idx2 >= 0) a_rcol2 = ivec[idx2];
                AtA[c*m + col2] += a_rc * a_rcol2;
            }
            // AtE
            if (r == epos){
                AtE[c] += a_rc * 1.0f; // e[r] = 1 at epos
            }
        }
    }
    // add regularization lambda on diagonal
    for (int i=0;i<m;i++) AtA[i*m + i] += reg_lambda;

    // solve AtA * f = AtE
    float *f = malloc(sizeof(float)*m);
    int rc = solve_linear_system(AtA, AtE, f, m);
    if (rc != 0){
        ESP_LOGW(TAG,"deconv solve failed %d", rc);
        free(ivec); free(vvec); free(AtA); free(AtE); free(f);
        return ESP_ERR_INVALID_STATE;
    }

    // compute vf = sum_j f_j * v_{n-1 - j} (convolution aligned)
    // compute uf = sum_j f_j * 1 (since u is step -> convolution of f with step = cumulative sum of f).
    float vf = 0.0f, uf = 0.0f;
    for (int j=0;j<m;j++){
        int vidx = (n-1) - j;
        float vval = (vidx >= 0) ? vvec[vidx] : vvec[0]; // guard
        vf += f[j] * vval;
        uf += f[j] * 1.0f;
    }
    float ocv_est = (uf != 0.0f) ? (vf / uf) : vvec[n-1] + h->i_buf[(start + n -1)%h->window_size]*h->r_int;
    // Convert OCV -> SOC: here we don't have OCV table, so we'll just compute delta compared to terminal
    // Real implementation should map ocv_est via OCV->SOC table (per battery chemistry).
    // As fallback, use IR compensation:
    float vterm = vvec[n-1];
    float v_oc_ir = vterm + h->i_buf[(start + n -1)%h->window_size]*h->r_int;

    // Choose final OCV (prefers deconv but check bounds)
    float final_ocv = ocv_est;
    if (!isfinite(final_ocv) || fabsf(final_ocv - vterm) > 2.0f) final_ocv = v_oc_ir;

    // We cannot map to SOC without OCV->SOC curve. Instead we compute a correction:
    // If deconv suggests higher OCV than IR estimate, we trust it a bit and adjust SOC_cc by blending.
    float delta_v = final_ocv - vterm;
    // translate delta_v to approx delta_soc using an empirical slope (dV/dSOC). Use 0.01 V per % as default (adjustable).
    float dv_per_soc = 0.01f; // V per percent of SOC (very approximate!)
    float delta_soc = (delta_v / dv_per_soc) / 100.0f;
    // apply blending and safety clamps
    float alpha = 0.9f; // trust universal strongly
    float new_soc = h->soc_cc + alpha * delta_soc;
    if (new_soc > 1.0f) new_soc = 1.0f;
    if (new_soc < 0.0f) new_soc = 0.0f;
    ESP_LOGI(TAG,"univ ocv=%.3f vterm=%.3f dv=%.3f => delta_soc=%.3f%% new_soc=%.2f%%",
             final_ocv, vterm, delta_v, delta_soc*100.0f, new_soc*100.0f);
    h->soc_cc = new_soc;

    free(ivec); free(vvec); free(AtA); free(AtE); free(f);
    return ESP_OK;
}

// persistence stubs (implement using nvs_flash as you want)
esp_err_t bsoc_store_to_nvs(battery_soc_t *h) { (void)h; return ESP_OK; }
esp_err_t bsoc_load_from_nvs(battery_soc_t *h)  { (void)h; return ESP_OK; }
