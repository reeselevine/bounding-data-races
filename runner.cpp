#include <map>
#include <set>
#include <iostream>
#include <string>
#include <sstream>
#include <fstream>
#include <chrono>
#include <easyvk.h>
#include <unistd.h>

using namespace std;
using namespace easyvk;

/** Returns the GPU to use for this test run. Users can specify the specific GPU to use
 *  with the 'gpuDeviceId' parameter. If gpuDeviceId is not included in the parameters or the specified
 *  device cannot be found, the first device is used.
 */
Device getDevice(Instance &instance, int device_id) {
  int idx = 0;
  int j = 0;
  for (Device _device : instance.devices()) {
    if (_device.properties().deviceID == device_id) {
      idx = j;
	    break;
    }
    j++;
  }
  Device device = instance.devices().at(idx);
  cout << "Using device " << device.properties().deviceName << "\n";
  return device;
}

void listDevices() {
  auto instance = Instance(false);
  for (Device _device : instance.devices()) {
    cout << "Device: " << device.properties().deviceName << " ID: " << device.properties().deviceID << "\n";
  }
}

/** Zeroes out the specified buffer. */
void clearMemory(Buffer &gpuMem, int size) {
  for (int i = 0; i < size; i++) {
    gpuMem.store(i, 0);
  }
}

/** Checks whether a random value is less than a given percentage. Used for parameters like memory stress that should only
 *  apply some percentage of iterations.
 */
bool percentageCheck(int percentage) {
  return rand() % 100 < percentage;
}

/** Assigns shuffled workgroup ids, using the shufflePct to determine whether the ids should be shuffled this iteration. */
void setShuffledWorkgroups(Buffer &shuffledWorkgroups, int numWorkgroups, int shufflePct) {
  for (int i = 0; i < numWorkgroups; i++) {
    shuffledWorkgroups.store(i, i);
  }
  if (percentageCheck(shufflePct)) {
    for (int i = numWorkgroups - 1; i > 0; i--) {
      int swap = rand() % (i + 1);
      int temp = shuffledWorkgroups.load(i);
      shuffledWorkgroups.store(i, shuffledWorkgroups.load(swap));
      shuffledWorkgroups.store(swap, temp);
    }
  }
}

/** Sets the stress regions and the location in each region to be stressed. Uses the stress assignment strategy to assign
  * workgroups to specific stress locations. Assignment strategy 0 corresponds to a "round-robin" assignment where consecutive
  * threads access separate scratch locations, while assignment strategy 1 corresponds to a "chunking" assignment where a group
  * of consecutive threads access the same location.
  */
void setScratchLocations(Buffer &locations, int numWorkgroups, map<string, int> params) {
  set <int> usedRegions;
  int numRegions = params["scratchMemorySize"] / params["stressLineSize"];
  for (int i = 0; i < params["stressTargetLines"]; i++) {
    int region = rand() % numRegions;
    while(usedRegions.count(region))
      region = rand() % numRegions;
    int locInRegion = rand() % (params["stressLineSize"]);
    switch (params["stressAssignmentStrategy"]) {
      case 0:
        for (int j = i; j < numWorkgroups; j += params["stressTargetLines"]) {
          locations.store(j, (region * params["stressLineSize"]) + locInRegion);
        }
        break;
      case 1:
        int workgroupsPerLocation = numWorkgroups/params["stressTargetLines"];
        for (int j = 0; j < workgroupsPerLocation; j++) {
          locations.store(i*workgroupsPerLocation + j, (region * params["stressLineSize"]) + locInRegion);
        }
        if (i == params["stressTargetLines"] - 1 && numWorkgroups % params["stressTargetLines"] != 0) {
          for (int j = 0; j < numWorkgroups % params["stressTargetLines"]; j++) {
            locations.store(numWorkgroups - j - 1, (region * params["stressLineSize"]) + locInRegion);
          }
        }
        break;
    }
  }
}

/** These parameters vary per iteration, based on a given percentage. */
void setDynamicStressParams(Buffer &stressParams, map<string, int> params) {
  if (percentageCheck(params["barrierPct"])) {
    stressParams.store(0, 1);
  } else {
    stressParams.store(0, 0);
  }  
  if (percentageCheck(params["memStressPct"])) {
    stressParams.store(1, 1);
  } else {
    stressParams.store(1, 0);
  }  
  if (percentageCheck(params["preStressPct"])) {
    stressParams.store(4, 1);
  } else {
    stressParams.store(4, 0);
  }
}

/** These parameters are static for all iterations of the test. Aliased memory is used for coherence tests. */
void setStaticStressParams(Buffer &stressParams, map<string, int> params) {
  stressParams.store(2, params["memStressIterations"]);
  stressParams.store(3, params["memStressPattern"]);
  stressParams.store(5, params["preStressIterations"]);
  stressParams.store(6, params["preStressPattern"]);
  stressParams.store(7, params["permuteFirst"]);
  stressParams.store(8, params["permuteSecond"]);
  stressParams.store(9, params["testingWorkgroups"]);
  stressParams.store(10, params["memStride"]);
  if (params["aliasedMemory"] == 1) {
    stressParams.store(11, 0);
  } else {
    stressParams.store(11, params["memStride"]);
  }
}

/** Returns a value between the min and max. */
int setBetween(int min, int max) {
  if (min == max) {
    return min;
  } else {
    int size = rand() % (max - min);
    return min + size;
  }
}

/** A test consists of N iterations of a shader and its corresponding result shader. */
void run(string &shader_file, string &result_shader_file, map<string, int> params, int device_id, bool enable_validation_layers)
{
  // initialize settings
  auto instance = Instance(enable_validation_layers);
  auto device = getDevice(instance, device_id);
  int workgroupSize = setBetween(params["minWorkgroupSize"], params["maxWorkgroupSize"]);
  int testingThreads = workgroupSize * params["testingWorkgroups"];
  int testLocSize = testingThreads * params["memStride"];

  // set up buffers
  auto nonAtomicTestLocations = Buffer(device, testLocSize);
  auto atomicTestLocations = Buffer(device, testLocSize);
  auto readResults = Buffer(device, params["numOutputs"] * testingThreads);
  auto testResults = Buffer(device, 2);
  auto shuffledWorkgroups = Buffer(device, params["maxWorkgroups"]);
  auto barrier = Buffer(device, 1);
  auto scratchpad = Buffer(device, params["scratchMemorySize"]);
  auto scratchLocations = Buffer(device, params["maxWorkgroups"]);
  auto stressParams = Buffer(device, 12);
  setStaticStressParams(stressParams, params);
  vector<Buffer> buffers = {nonAtomicTestLocations, atomicTestLocations, readResults, shuffledWorkgroups, barrier, scratchpad, scratchLocations, stressParams};
  vector<Buffer> resultBuffers = {nonAtomicTestLocations, readResults, testResults, stressParams};

  // run iterations
  chrono::time_point<std::chrono::system_clock> start, end;
  start = chrono::system_clock::now();
  for (int i = 0; i < params["testIterations"]; i++) {
    auto program = Program(device, shader_file.c_str(), buffers);
    auto resultProgram = Program(device, result_shader_file.c_str(), resultBuffers);
    int numWorkgroups = setBetween(params["testingWorkgroups"], params["maxWorkgroups"]);
    clearMemory(nonAtomicTestLocations, testLocSize);
    clearMemory(atomicTestLocations, testLocSize);
    clearMemory(testResults, 2);
    clearMemory(barrier, 1);
    clearMemory(scratchpad, params["scratchMemorySize"]);
    setShuffledWorkgroups(shuffledWorkgroups, numWorkgroups, params["shufflePct"]);
    setScratchLocations(scratchLocations, numWorkgroups, params);
    setDynamicStressParams(stressParams, params);
    program.setWorkgroups(numWorkgroups);
    resultProgram.setWorkgroups(params["testingWorkgroups"]);
    program.setWorkgroupSize(workgroupSize);
    resultProgram.setWorkgroupSize(workgroupSize);
    program.prepare();
    program.run();
    resultProgram.prepare();
    resultProgram.run();
    cout << "Iteration " << i << "\n";
    cout << "Not bounded: " << testResults.load(0) << "\n";
    cout << "Bounded: " << testResults.load(1) << "\n";
    cout << "\n";
    program.teardown();
  }
  for (Buffer buffer : buffers) {
    buffer.teardown();
  }
  testResults.teardown();
  device.teardown();
  instance.teardown();
}

/** Reads a specified config file and stores the parameters in a map. Parameters should be of the form "key=value", one per line. */
map<string, int> read_config(string &config_file)
{
  map<string, int> m;
  ifstream in_file(config_file);
  string line;
  while (getline(in_file, line))
  {
    istringstream is_line(line);
    string key;
    if (getline(is_line, key, '='))
    {
      string value;
      if (getline(is_line, value))
      {
        m[key] = stoi(value);
      }
    }
  }
  return m;
}

int main(int argc, char *argv[])
{

  string shaderFile;
  string shaderResultFile;
  string paramFile;
  int deviceID = 0;
  bool enableValidationLayers = false;
  bool listDevices = false;
  
  int c;
  while ((c = getopt(argc, argv, "vcls:r:p:")) != -1)
    switch (c)
    {
    case 's':
      shaderFile = optarg;
      break;
    case 'r':
      shaderResultFile = optarg;
      break;
    case 'p':
      paramFile = optarg;
    case 'v':
      enableValidationLayers = true;
      break;
    case 'l':
      listDevices = true;
      break;
    case 'd':
      deviceID = atoi(optarg);
      break;
    case '?':
      if (optopt == 's' || optopt == 'r' || optopt == 'p')
        std::cerr << "Option -" << optopt << "requires an argument\n";
      else
        std::cerr << "Unknown option" << optopt << "\n";
      return 1;
    default:
      abort();
    }
  
  srand(time(NULL));
  map<string, int> params = read_config(paramFile);
  for (const auto& [key, value] : params) {
    std::cout << key << " = " << value << "; ";
  }
  std::cout << "\n";
  run(shaderFile, resultShaderFile, deviceID, enableValidationLayers);
  return 0;
}
