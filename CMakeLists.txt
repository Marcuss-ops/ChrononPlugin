cmake_minimum_required(VERSION 3.20)
project(ChrononPlugin LANGUAGES CXX)

# Livello C dell'architettura Chronon: catalogo dei plugin specializzati.
# Ogni plugin è una libreria/tool autonomo; il registry canonico e i contratti
# di estensione restano in Chronon3D (ExtensionModule/ExtensionContext).
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

enable_testing()

add_subdirectory(plugins/maps)
