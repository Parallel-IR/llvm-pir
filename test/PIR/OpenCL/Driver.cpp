#include <CL/cl.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <map>

#define OCL_DRIVER_SUCCESS             0
#define OCL_DRIVER_NO_PLATFORM         -1
#define OCL_DRIVER_NO_DEVICE           -2
#define OCL_DRIVER_CONTEXT_ERROR       -3
#define OCL_DRIVER_COMMAND_QUEUE_ERROR -4
#define OCL_DRIVER_PROGRAM_BUILD_ERROR -5
#define OCL_DRIVER_KERNEL_ERROR        -6

struct OCLKernelArgInfo {
  enum ArgType { Scalar = 0, Array = 1 };

  void *Ptr;
  size_t Size;
  ArgType Type;
};
 
class OCLDriver {
public:
  // Searches for the first device that matches the provided \p Type and
  // contructs cl::Context and cl::CommandQueue for that device.
  OCLDriver(cl_device_type Type) : Type(Type) {
    getFirstOCLDevice();
    createOCLContextForDevice();
    createOCLCommandQueueForDevice();
  }

  int buildProgram(std::istream &&InputStream, cl::Program &Program) {
    std::string Src(std::istreambuf_iterator<char>(InputStream), {});

    std::cout << "Building OpenCL program: " << std::endl;
    std::cout << Src.c_str() << std::endl;

    cl::Program::Sources SrcVec(1, {Src.c_str(), Src.length()});
    Program = cl::Program(Context, SrcVec);
    cl_int Err = Program.build({Device});

    if (Err != CL_SUCCESS) {
      std::cerr << "Error while trying to build OpenCL program" << Err
                << std::endl;
      std::cerr << Program.getBuildInfo<CL_PROGRAM_BUILD_LOG>(Device)
                << std::endl;

      return OCL_DRIVER_PROGRAM_BUILD_ERROR;
    }

    std::cout << "Input program was built correctly" << std::endl << std::endl;
    return OCL_DRIVER_SUCCESS;
  }

  int executeKernel(const cl::Program &Program, const std::string KernelName,
                    int Dim, std::vector<OCLKernelArgInfo> Args) {
    cl_int Err;
    std::cout << "Kernel Name: " << KernelName.c_str() << std::endl;
    cl::Kernel Kernel(Program, KernelName.c_str(), &Err);

    if (Err != CL_SUCCESS) {
      std::cerr << "Error while creating a kernel object " << Err << std::endl;
      return OCL_DRIVER_KERNEL_ERROR;
    }

    std::map<void *, cl::Buffer> HostToDeviceMap;

    // Set the kernel arguments.
    for (int i = 0; i < Args.size(); ++i) {
      switch (Args[i].Type) {
      case OCLKernelArgInfo::ArgType::Scalar: {
        Err = Kernel.setArg(i, Args[i].Size, Args[i].Ptr);
        break;
      }
      case OCLKernelArgInfo::ArgType::Array: {
        // Allocate device buffer to hold the argument data.
        cl::Buffer ArgBuffer(Context, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR,
                             Args[i].Size, Args[i].Ptr, &Err);

        if (Err != CL_SUCCESS) {
          std::cerr << "Error while allocating cl_mem buffer for a kernel "
                       "argument (index "
                    << i << ") " << Err << std::endl;
          return OCL_DRIVER_KERNEL_ERROR;
        }

        Err = Kernel.setArg(i, sizeof(cl_mem), &ArgBuffer);

        HostToDeviceMap[Args[i].Ptr] = ArgBuffer;
        break;
      }
      }

      if (Err != CL_SUCCESS) {
        std::cerr << "Error while setting a kernel argument (index " << i
                  << ") " << Err << std::endl;
        return OCL_DRIVER_KERNEL_ERROR;
      }

      std::cout << "Argument at index " << i << " is set correcly" << std::endl;
    }

    cl::Event Event;
    Err = CommandQueue.enqueueNDRangeKernel(
        Kernel, cl::NullRange, cl::NDRange(Dim), cl::NullRange, NULL, &Event);

    if (Err != CL_SUCCESS) {
      std::cerr << "Error while executing a kernel object " << Err << std::endl;
      return OCL_DRIVER_KERNEL_ERROR;
    }

    std::cout << "The requested kernel was launched for execution" << std::endl;
    Event.wait();

    // Read the device buffers back into the original host ptrs.
    for (int i=0 ; i<Args.size() ; ++i) {
      switch (Args[i].Type) {
        case OCLKernelArgInfo::ArgType::Scalar : { break; }
        case OCLKernelArgInfo::ArgType::Array : {
          // Allocate device buffer to hold the argument data.
          cl::Buffer ArgBuffer = HostToDeviceMap[Args[i].Ptr];
          Err = CommandQueue.enqueueReadBuffer(ArgBuffer, true, 0, Args[i].Size,
                                               Args[i].Ptr);

          if (Err != CL_SUCCESS) {
            std::cerr << "Error while copying kernel result (index " << i
                      << ") " << Err << std::endl;
          }

          break;
        }
      }

      if (Err != CL_SUCCESS) {
        std::cerr << "Error while setting a kernel argument (index " << i
                  << ") " << Err << std::endl;
        return OCL_DRIVER_KERNEL_ERROR;
      }
    }

    return OCL_DRIVER_SUCCESS;
  }

  void dump() const {
    std::string Header;

    switch (Type) {
    case CL_DEVICE_TYPE_GPU: {
      Header = "Results for OpenCL GPU device selection: ";
      break;
    }
    case CL_DEVICE_TYPE_CPU: {
      Header = "Results for OpenCL CPU device selection: ";
      break;
    }
    case CL_DEVICE_TYPE_ALL: {
      Header = "Results for OpenCL device selection: ";
      break;
    }
    case CL_DEVICE_TYPE_DEFAULT: {
      Header = "Results for OpenCL default device selection: ";
      break;
    }
    }

    std::cout << Header << std::endl;
    std::cout << "\tSelected Platform: " << Platform.getInfo<CL_PLATFORM_NAME>()
              << std::endl;
    std::cout << "\tSelected Device: " << Device.getInfo<CL_DEVICE_NAME>()
              << std::endl;
    std::cout << "\tCreated Context info: " << std::endl;
    std::cout << "\t\tNumber of devices: "
              << Context.getInfo<CL_CONTEXT_NUM_DEVICES>() << std::endl;
    std::cout << std::endl;
  }

private:
  int getFirstOCLDevice() {
    std::vector<cl::Platform> Platforms;
    cl::Platform::get(&Platforms);

    if (Platforms.size() == 0) {
      std::cerr << "No OpenCL platforms were found" << std::endl;
      return OCL_DRIVER_NO_PLATFORM;
    }

    for (auto &P : Platforms) {
      std::vector<cl::Device> Devices;
      P.getDevices(Type, &Devices);
      if (Devices.size() > 0) {
        Device = Devices[0];
        Platform = P;
        return OCL_DRIVER_SUCCESS;
      }
    }

    std::cerr << "Couldn't find requested device" << std::endl;

    return OCL_DRIVER_NO_DEVICE;
  }

  int createOCLContextForDevice() {
    cl_int Err;
    cl_context_properties Properties[] = {
        CL_CONTEXT_PLATFORM, (cl_context_properties)(Platform)(), 0};
    Context = cl::Context(Device, Properties, NULL, NULL, &Err);

    if (Err != CL_SUCCESS) {
      std::cerr << "Error while creating OpenCL context " << Err << std::endl;
      return OCL_DRIVER_CONTEXT_ERROR;
    }

    return OCL_DRIVER_SUCCESS;
  }

  int createOCLCommandQueueForDevice() {
    cl_int Err;
    CommandQueue = cl::CommandQueue(Context, Device, 0, &Err);

    if (Err != CL_SUCCESS) {
      std::cerr << "Error while creating OpenCL context " << Err << std::endl;
      return OCL_DRIVER_COMMAND_QUEUE_ERROR;
    }

    return OCL_DRIVER_SUCCESS;
  }

private:
  cl_device_type Type;
  cl::Device Device;
  cl::Platform Platform;
  cl::Context Context;
  cl::CommandQueue CommandQueue;
};

extern "C" {
void invokeDriver2(cl_device_type Type, std::istream &&ProgramSrc,
                  std::string KernelName, int Dim,
                  std::vector<OCLKernelArgInfo> ArgInfos) {
  OCLDriver Driver(Type);
  Driver.dump();
  cl::Program Program;
  Driver.buildProgram(std::move(ProgramSrc), Program);
  Driver.executeKernel(Program, KernelName, Dim, ArgInfos);
}

void invokeDriver(const char *KernelFilePath, const char *KernelName, int Dim,
                   int NumArgs, void **ArgPtrs, int *ArgSizes, int *ArgTypes) {
  std::vector<OCLKernelArgInfo> ArgInfos(NumArgs);

  for (int i = 0; i < NumArgs; ++i) {
    ArgInfos[i] = {ArgPtrs[i], (size_t)ArgSizes[i],
                   (OCLKernelArgInfo::ArgType)ArgTypes[i]};
  }

  invokeDriver2(CL_DEVICE_TYPE_GPU, std::ifstream(KernelFilePath), KernelName,
               Dim, ArgInfos);
}
}
