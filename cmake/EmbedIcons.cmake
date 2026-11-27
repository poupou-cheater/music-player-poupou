file(GLOB SVG_FILES "${ICONS_DIR}/*.svg")
list(SORT SVG_FILES)

get_filename_component(OUT_DIR "${OUT_FILE}" DIRECTORY)
file(MAKE_DIRECTORY "${OUT_DIR}")

set(CONTENT "#pragma once\n#include <string_view>\n#include <utility>\n\ninline constexpr std::pair<std::string_view, std::string_view> kEmbeddedIcons[] = {\n")

foreach(SVG_PATH ${SVG_FILES})
    get_filename_component(STEM "${SVG_PATH}" NAME_WE)
    file(READ "${SVG_PATH}" SVG_DATA)
    string(APPEND CONTENT "    { \"${STEM}\", R\"__SVG__(${SVG_DATA})__SVG__\" },\n")
endforeach()

string(APPEND CONTENT "};\n")
file(WRITE "${OUT_FILE}" "${CONTENT}")
