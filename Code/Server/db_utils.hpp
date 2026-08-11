#ifndef DB_UTILS_HPP
#define DB_UTILS_HPP

#include <string>

bool init_database(const std::string& db_path);
bool insert_message(const std::string& db_path, 
                    const std::string& name,
                    const std::string& message);

#endif                    
