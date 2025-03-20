#include "src/kernel/fmu/conf.hpp"
#include <xbt/file.hpp>
#include <xbt/parse_units.hpp>
#include <string>
#include <simgrid/Exception.hpp>

std::string simgrid_parsed_file;
static FILE* file_to_parse = nullptr;

void parse_fmuconf(const std::string& file)
{
    simgrid_parse_fmu(file);
}

void simgrid_parse_fmu(const std::string&file)
{
    simgrid_parsed_file = file;
    std::string dir = simgrid::xbt::Path(file).get_dir_name();
    simgrid::xbt::path_push(dir);

    file_to_parse = simgrid::xbt::path_fopen(file, "r");

    if (file_to_parse == nullptr )
        throw std::invalid_argument("Unable to open '" + file + "' from '" + simgrid::xbt::Path().get_name() +
                                "'. Does this file exist?");
    
}