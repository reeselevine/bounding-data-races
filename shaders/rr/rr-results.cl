typedef struct TestResults {
  atomic_uint not_bounded;
  atomic_uint bounded;
} TestResults;

__kernel void check_results (
  __global atomic_uint* read_results,
  __global TestResults* test_results,
  __global uint* stress_params) {
  uint id_0 = get_global_id(0);
  uint flag = atomic_load(&read_results[id_0 * 3]); // flag
  uint r0 = atomic_load(&read_results[id_0 * 3 + 1]); // first read
  uint r1 = atomic_load(&read_results[id_0 * 3 + 2]); // second read
  if (flag == 1 && r0 != r1) {
    atomic_fetch_add(&test_results->not_bounded, 1);
  } else {
    atomic_fetch_add(&test_results->bounded, 1);
  }
}
    