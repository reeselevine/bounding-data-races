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
  auto physicalDevices = instance.physicalDevices();
//  for (auto physicalDevice : physicalDevices) {
//    Device _device = Device(instance, physicalDevice);
//    if (_device.properties.deviceID == device_id) {
//      idx = j;
//      break;
//    }
//    j++;
//  }
  Device device = Device(instance, physicalDevices.at(idx));
  cout << "Using device " << device.properties.deviceName << "\n";
  return device;
}

void listDevices() {
  auto instance = Instance(false);
  for (auto physicalDevice : instance.physicalDevices()) {
    Device device = Device(instance, physicalDevice);
    cout << "Device: " << device.properties.deviceName << " ID: " << device.properties.deviceID << "\n";
  }
}

/** Zeroes out the specified buffer. */
void clearMemory(Buffer &gpuMem, int size) {
  for (int i = 0; i < size; i++) {
    gpuMem.store<uint32_t>(i, 0);
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
    shuffledWorkgroups.store<uint32_t>(i, i);
  }
  if (percentageCheck(shufflePct)) {
    for (int i = numWorkgroups - 1; i > 0; i--) {
      int swap = rand() % (i + 1);
      int temp = shuffledWorkgroups.load<uint32_t>(i);
      shuffledWorkgroups.store<uint32_t>(i, shuffledWorkgroups.load<uint32_t>(swap));
      shuffledWorkgroups.store<uint32_t>(swap, temp);
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
          locations.store<uint32_t>(j, (region * params["stressLineSize"]) + locInRegion);
        }
        break;
      case 1:
        int workgroupsPerLocation = numWorkgroups/params["stressTargetLines"];
        for (int j = 0; j < workgroupsPerLocation; j++) {
          locations.store<uint32_t>(i*workgroupsPerLocation + j, (region * params["stressLineSize"]) + locInRegion);
        }
        if (i == params["stressTargetLines"] - 1 && numWorkgroups % params["stressTargetLines"] != 0) {
          for (int j = 0; j < numWorkgroups % params["stressTargetLines"]; j++) {
            locations.store<uint32_t>(numWorkgroups - j - 1, (region * params["stressLineSize"]) + locInRegion);
          }
        }
        break;
    }
  }
}

/** These parameters vary per iteration, based on a given percentage. */
void setDynamicStressParams(Buffer &stressParams, map<string, int> params) {
  if (percentageCheck(params["barrierPct"])) {
    stressParams.store<uint32_t>(0, 1);
  } else {
    stressParams.store<uint32_t>(0, 0);
  }  
  if (percentageCheck(params["memStressPct"])) {
    stressParams.store<uint32_t>(1, 1);
  } else {
    stressParams.store<uint32_t>(1, 0);
  }  
  if (percentageCheck(params["preStressPct"])) {
    stressParams.store<uint32_t>(4, 1);
  } else {
    stressParams.store<uint32_t>(4, 0);
  }
}

/** These parameters are static for all iterations of the test. Aliased memory is used for coherence tests. */
void setStaticStressParams(Buffer &stressParams, map<string, int> params) {
  stressParams.store<uint32_t>(2, params["memStressIterations"]);
  stressParams.store<uint32_t>(3, params["memStressPattern"]);
  stressParams.store<uint32_t>(5, params["preStressIterations"]);
  stressParams.store<uint32_t>(6, params["preStressPattern"]);
  stressParams.store<uint32_t>(7, params["permuteFirst"]);
  stressParams.store<uint32_t>(8, params["permuteSecond"]);
  stressParams.store<uint32_t>(9, params["testingWorkgroups"]);
  stressParams.store<uint32_t>(10, params["memStride"]);
  if (params["aliasedMemory"] == 1) {
    stressParams.store<uint32_t>(11, 0);
  } else {
    stressParams.store<uint32_t>(11, params["memStride"]);
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
  int testingThreads = params["workgroupSize"] * params["testingWorkgroups"];
  int testLocSize = testingThreads * params["memStride"];

  // set up buffers
  vector<Buffer> buffers;
  vector<Buffer> resultBuffers;
  if (!params["workgroupMemory"] == 1 || params["checkMemory"] == 1) { // test shader needs a non atomic buffer for device buffers, or if we need to save workgroup memory
    auto nonAtomicTestLocations = Buffer(device, testLocSize, sizeof(uint32_t));
    buffers.push_back(nonAtomicTestLocations);
    if (params["checkMemory"] == 1) { // result shader only needs these locations if we need to check memory
      resultBuffers.push_back(nonAtomicTestLocations);
    }
  }
  if (!params["workgroupMemory"] == 1) { // test shader needs an atomic buffer only if it's not workgroup memory
    buffers.push_back(Buffer(device, testLocSize, sizeof(uint32_t))); // atomic test locations
  }

  auto readResults = Buffer(device, params["numOutputs"] * testingThreads, sizeof(uint32_t));
  buffers.push_back(readResults);
  resultBuffers.push_back(readResults);
  auto testResults = Buffer(device, 2, sizeof(uint32_t));
  resultBuffers.push_back(testResults);
  auto shuffledWorkgroups = Buffer(device, params["maxWorkgroups"], sizeof(uint32_t));
  buffers.push_back(shuffledWorkgroups);
  auto barrier = Buffer(device, 1, sizeof(uint32_t));
  buffers.push_back(barrier);
  auto scratchpad = Buffer(device, params["scratchMemorySize"], sizeof(uint32_t));
  buffers.push_back(scratchpad);
  auto scratchLocations = Buffer(device, params["maxWorkgroups"], sizeof(uint32_t));
  buffers.push_back(scratchLocations);
  auto stressParams = Buffer(device, 12, sizeof(uint32_t));
  setStaticStressParams(stressParams, params);
  buffers.push_back(stressParams);
  resultBuffers.push_back(stressParams);

  // run iterations
  chrono::time_point<std::chrono::system_clock> start, end;
  start = chrono::system_clock::now();
  for (int i = 0; i < params["testIterations"]; i++) {
    auto program = Program(device, shader_file.c_str(), buffers);
    auto resultProgram = Program(device, result_shader_file.c_str(), resultBuffers);

    int numWorkgroups = setBetween(params["testingWorkgroups"], params["maxWorkgroups"]);
    if (!params["workgroupMemory"] == 1 || params["checkMemory"] == 1) {
      clearMemory(buffers[0], testLocSize); // non-atomic test locations will be the first buffer
    }
    if (!params["workgroupMemory"] == 1) {
      clearMemory(buffers[1], testLocSize); // atomic test locations will be the second buffer
    }
    clearMemory(testResults, 2);
    clearMemory(barrier, 1);
    clearMemory(scratchpad, params["scratchMemorySize"]);
    setShuffledWorkgroups(shuffledWorkgroups, numWorkgroups, params["shufflePct"]);
    setScratchLocations(scratchLocations, numWorkgroups, params);
    setDynamicStressParams(stressParams, params);

    program.setWorkgroups(numWorkgroups);
    resultProgram.setWorkgroups(params["testingWorkgroups"]);
    program.setWorkgroupSize(params["workgroupSize"]);
    resultProgram.setWorkgroupSize(params["workgroupSize"]);

    // workgroup memory shaders use workgroup memory for testing
    if (params["workgroupMemory"] == 1) {
      program.setWorkgroupMemoryLength(testLocSize*sizeof(uint32_t), 0);
      program.setWorkgroupMemoryLength(testLocSize*sizeof(uint32_t), 1);
    }

    program.initialize("run_test");
    program.run();
    resultProgram.initialize("check_results");
    resultProgram.run();

    cout << "Iteration " << i << "\n";
    for (int i = 0; i < testingThreads; i++) {
      cout << "flag: " << readResults.load<uint32_t>(i*3) << " r0: " << readResults.load<uint32_t>(i*3 + 1) << " r1: " << readResults.load<uint32_t>(i*3 + 2) << "\n";
    }
    cout << "Not bounded: " << testResults.load<uint32_t>(0) << "\n";
    cout << "Bounded: " << testResults.load<uint32_t>(1) << "\n";
    cout << "\n";
    program.teardown();
    resultProgram.teardown();
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
  string resultShaderFile;
  string paramFile;
  int deviceID = 0;
  bool enableValidationLayers = false;
  bool list_devices = false;
  
  int c;
  while ((c = getopt(argc, argv, "vcls:r:p:")) != -1)
    switch (c)
    {
    case 's':
      shaderFile = optarg;
      break;
    case 'r':
      resultShaderFile = optarg;
      break;
    case 'p':
      paramFile = optarg;
    case 'v':
      enableValidationLayers = true;
      break;
    case 'l':
      list_devices = true;
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

  if (list_devices) {
    listDevices();
    return 0;
  }

  if (shaderFile.empty()) {
    std::cerr << "Shader (-s) must be set\n";
    return 1;
  }    

   if (resultShaderFile.empty()) {
    std::cerr << "Result shader (-r) must be set\n";
    return 1;
  }    

  if (paramFile.empty()) {
    std::cerr << "Param file (-p) must be set\n";
    return 1;
  }    
 
  srand(time(NULL));
  map<string, int> params = read_config(paramFile);
  for (const auto& [key, value] : params) {
    std::cout << key << " = " << value << "; ";
  }
  std::cout << "\n";
  run(shaderFile, resultShaderFile, params, deviceID, enableValidationLayers);
  return 0;
}
