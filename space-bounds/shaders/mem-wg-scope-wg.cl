static uint permute_id(uint id, uint factor, uint mask) {
  return (id * factor) % mask;
}

static void spin(__global atomic_uint* barrier, uint limit) {
  int i = 0;
  uint val = atomic_fetch_add_explicit(barrier, 1, memory_order_relaxed);
  while (i < 1024 && val < limit) {
    val = atomic_load_explicit(barrier, memory_order_relaxed);
    i++;
  }
}

static void do_stress(__global uint* scratchpad, __global uint* scratch_locations, uint iterations, uint pattern) {
  for (uint i = 0; i < iterations; i++) {
    if (pattern == 0) {
      scratchpad[scratch_locations[get_group_id(0)]] = i;
      scratchpad[scratch_locations[get_group_id(0)]] = i + 1;
    } else if (pattern == 1) {
      scratchpad[scratch_locations[get_group_id(0)]] = i;
      uint tmp1 = scratchpad[scratch_locations[get_group_id(0)]];
      if (tmp1 > 100) {
        break;
      }
    } else if (pattern == 2) {
      uint tmp1 = scratchpad[scratch_locations[get_group_id(0)]];
      if (tmp1 > 100) {
        break;
      }
      scratchpad[scratch_locations[get_group_id(0)]] = i;
    } else if (pattern == 3) {
      uint tmp1 = scratchpad[scratch_locations[get_group_id(0)]];
      if (tmp1 > 100) {
        break;
      }
      uint tmp2 = scratchpad[scratch_locations[get_group_id(0)]];
      if (tmp2 > 100) {
        break;
      }
    }
  }
}

__kernel void run_test (
  __global uint* x_locations,
  __global uint* y_locations,
  __local uint* wg_x_locations,
  __local uint* wg_y_locations,
  __global uint* shuffled_workgroups,
  __global atomic_uint* _barrier,
  __global uint* scratchpad,
  __global uint* scratch_locations,
  __global uint* stress_params) {

  uint shuffled_workgroup = shuffled_workgroups[get_group_id(0)];
  if(shuffled_workgroup < stress_params[9]) {
    uint total_ids = get_local_size(0);
    uint id_0 = get_local_id(0);
    uint id_1 = permute_id(get_local_id(0), stress_params[7], get_local_size(0));
    uint x_0 = id_0 * stress_params[10]; // used to write to location x (thread 0)
    uint y_0 = id_0 * stress_params[10]; // used to write to location y (thread 0)
    uint x_1 = id_1 * stress_params[10]; // used to write to location x (thread 1)
    if (stress_params[4]) {
      do_stress(scratchpad, scratch_locations, stress_params[5], stress_params[6]);
    }
    if (stress_params[0]) {
      spin(_barrier, get_local_size(0));
    }

    // Thread 1
    wg_x_locations[x_1] = 1;

    // Thread 0
    uint a = stress_params[8];
    wg_x_locations[x_0] = a + 10;
    // try some stuff out
    wg_y_locations[y_0] = a + 10;

    x_locations[(shuffled_workgroup * get_local_size(0) + id_0) * stress_params[10]] = wg_x_locations[x_0];
    y_locations[(shuffled_workgroup * get_local_size(0) + id_0) * stress_params[10]] = wg_y_locations[y_0];
  } else if (stress_params[1]) {
    do_stress(scratchpad, scratch_locations, stress_params[2], stress_params[3]);
  }
}
