typedef struct TestResults {
  atomic_uint seq0;
  atomic_uint seq1;
  atomic_uint not_bounded0;
  atomic_uint not_bounded1;
  atomic_uint other;
} TestResults;

__kernel void check_results (
  __global uint* x_locations,
  __global uint* y_locations,
  __global TestResults* test_results,
  __global uint* stress_params) {
  uint id_0 = get_global_id(0);
  uint x_0 = id_0 * stress_params[10];
  uint y_0 = id_0 * stress_params[10];
  uint x_val = x_locations[x_0];
  uint y_val = y_locations[y_0];
  if (x_val == 1 && y_val == 42) {
    atomic_fetch_add(&test_results->seq0, 1);
  } else if (x_val == 42 && y_val == 42) {
     atomic_fetch_add(&test_results->seq1, 1);
  } else if (x_val == 1 && y_val == 1) {
     atomic_fetch_add(&test_results->not_bounded0, 1);
  } else if (x_val == 42 && y_val == 1) {
     atomic_fetch_add(&test_results->not_bounded1, 1);
  } else {
    atomic_fetch_add(&test_results->other, 1);
  }
}
