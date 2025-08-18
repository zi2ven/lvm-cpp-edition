#include <iostream>
#include <argparse/argparse.hpp>

#include "vm.h"

int read_file_to_buffer(const std::string& path, uint8_t*& raw, size_t& size)
{
    FILE* file = fopen(path.c_str(), "rb");
    if (!file)
    {
        return -1;
    }

    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);

    raw = static_cast<uint8_t*>(malloc(size));
    if (!raw)
    {
        fclose(file);
        return -2;
    }

    size_t bytes_read = fread(raw, 1, size, file);
    fclose(file);

    if (bytes_read != size)
    {
        free(raw);
        raw = nullptr;
        return -3;
    }

    return 0;
}

int main(int argc, const char** argv)
{
    argparse::ArgumentParser program("lvm");
    program.add_argument("file")
           .help("File to execute")
           // .required();
           .default_value("f.lvme");
    program.add_argument("--stack-size", "-s")
           .help("Stack size")
           .default_value(lvm::DEFAULT_STACK_SIZE);
    try
    {
        program.parse_args(argc, argv);
    }
    catch (const std::runtime_error& err)
    {
        std::cerr << err.what() << std::endl;
        std::cerr << program;
        return 1;
    }
    lvm::VirtualMachine vm(program.get<uint64_t>("--stack-size"));
    const std::string path = program.get("file");
    uint8_t* raw = nullptr;
    size_t size = 0;
    if (read_file_to_buffer(path, raw, size) != 0)
    {
        std::cerr << "Failed to read file" << std::endl;
        return 1;
    }
    const lvm::Module* module = lvm::Module::fromRaw(raw);
    free(raw);
    auto start = std::chrono::high_resolution_clock::now();
    vm.init(module);
    auto end = std::chrono::high_resolution_clock::now();
    auto duration1 = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    std::cout << "Init time: " << duration1.count() << " ms" << std::endl;
    auto rStart = std::chrono::high_resolution_clock::now();
    vm.run();
    auto rEnd = std::chrono::high_resolution_clock::now();
    auto duration2 = std::chrono::duration_cast<std::chrono::milliseconds>(rEnd - rStart);
    std::cout << "Execution time: " << duration2.count() << " ms" << std::endl;
    auto duration3 = std::chrono::duration_cast<std::chrono::milliseconds>(duration2 + duration1);
    std::cout << "Total time: " << duration3.count() << " ms" << std::endl;
    vm.destroy();
    return 0;
}
