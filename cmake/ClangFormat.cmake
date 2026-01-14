#
# Format and verify formatting using clang-format
#
cmake_minimum_required(VERSION 3.5)

include(FindPackageHandleStandardArgs)

if(NOT CLANG_FORMAT_EXE_NAME)
  set(CLANG_FORMAT_EXE_NAME clang-format)
endif()

if(CLANG_FORMAT_ROOT_DIR)
  find_program(CLANG_FORMAT_EXE
    NAMES ${CLANG_FORMAT_EXE_NAME}
    PATHS ${CLANG_FORMAT_ROOT_DIR}
    NO_DEFAULT_PATH)
endif()

find_program(CLANG_FORMAT_EXE NAMES ${CLANG_FORMAT_EXE_NAME})

find_package_handle_standard_args(CLANG_FORMAT DEFAULT_MSG CLANG_FORMAT_EXE)

mark_as_advanced(CLANG_FORMAT_EXE)

if(CLANG_FORMAT_FOUND)
  set(CLANG_FORMAT_FILE_EXTENSIONS ${CLANG_FORMAT_CXX_FILE_EXTENSIONS} *.cpp *.hpp *.c *.h)
  file(GLOB_RECURSE CLANG_FORMAT_ALL_SOURCE_FILES ${CLANG_FORMAT_FILE_EXTENSIONS})

  set(CLANG_FORMAT_EXCLUDE_PATTERNS ${CLANG_FORMAT_EXCLUDE_PATTERNS} "/CMakeFiles/" "cmake" "/build/" "/vendor/" "/third_party/" "cassandra.h" "dse.h")

  foreach(SOURCE_FILE ${CLANG_FORMAT_ALL_SOURCE_FILES})
    foreach(EXCLUDE_PATTERN ${CLANG_FORMAT_EXCLUDE_PATTERNS})
      string(FIND ${SOURCE_FILE} ${EXCLUDE_PATTERN} EXCLUDE_FOUND)
      if(NOT ${EXCLUDE_FOUND} EQUAL -1)
        list(REMOVE_ITEM CLANG_FORMAT_ALL_SOURCE_FILES ${SOURCE_FILE})
      endif()
    endforeach()
  endforeach()

  if(WIN32)
    set(CLANG_FORMAT_FILENAME "clang-format.files")
    set(CLANG_FORMAT_ABSOLUTE_FILENAME "${CMAKE_BINARY_DIR}/${CLANG_FORMAT_FILENAME}")
    if (EXISTS ${CLANG_FORMAT_ABSOLUTE_FILENAME})
      file(REMOVE ${CLANG_FORMAT_ABSOLUTE_FILENAME})
    endif()

    set(COUNT 1)
    file(TO_NATIVE_PATH ${CMAKE_BINARY_DIR} CMAKE_WINDOWS_BINARY_DIR)
    foreach(SOURCE_FILE ${CLANG_FORMAT_ALL_SOURCE_FILES})
      file(RELATIVE_PATH RELATIVE_SOURCE_FILE ${CMAKE_BINARY_DIR} ${SOURCE_FILE})
      file(TO_NATIVE_PATH ${RELATIVE_SOURCE_FILE} NATIVE_RELATIVE_SOURCE_FILE)

      if(COUNT EQUAL 50)
        file(APPEND ${CLANG_FORMAT_ABSOLUTE_FILENAME} " ${NATIVE_RELATIVE_SOURCE_FILE}\n")
        set(COUNT 1)
      else()
        file(APPEND ${CLANG_FORMAT_ABSOLUTE_FILENAME} " ${NATIVE_RELATIVE_SOURCE_FILE}")
        MATH(EXPR COUNT "${COUNT} + 1")
      endif()
    endforeach()

    file(TO_NATIVE_PATH ${CLANG_FORMAT_EXE} CLANG_FORMAT_EXE)
    file(WRITE "${CMAKE_BINARY_DIR}/clang-format-windows.bat"
               "@REM Generated clang-format script for Windows\r\n"
               "@ECHO OFF\r\n"
               "SETLOCAL ENABLEDELAYEDEXPANSION\r\n"
               "SET IS_FAILED_CHECK=0\r\n"
               "PUSHD ${CMAKE_WINDOWS_BINARY_DIR}>NUL\r\n"
               "FOR /F \"TOKENS=*\" %%A IN (${CLANG_FORMAT_FILENAME}) do (\r\n"
               "  IF %1 EQU 1 (\r\n"
               "    \"${CLANG_FORMAT_EXE}\" -i -fallback-style=none -style=file %%A\r\n"
               "    IF NOT !ERRORLEVEL! EQU 0 (\r\n"
               "      SET IS_FAILED_CHECK=1\r\n"
               "    )\r\n"
               "  )\r\n"
               "  IF %1 EQU 2 (\r\n"
               "    \"${CLANG_FORMAT_EXE}\" -output-replacements-xml -fallback-style=none -style=file %%A 2>&1 | FINDSTR /C:\"replacement offset\">NUL\r\n"
               "    IF !ERRORLEVEL! EQU 0 (\r\n"
               "      SET IS_FAILED_CHECK=1\r\n"
               "    )\r\n"
               "  )\r\n"
               ")\r\n"
               "IF NOT !IS_FAILED_CHECK! EQU 0 (\r\n"
               "  POPD\r\n"
               "  EXIT /B 1\r\n"
               ")\r\n"
               "POPD\r\n"
               "ENDLOCAL\r\n"
               "EXIT /B 0\r\n")

    add_custom_target(format
      COMMENT "Format source files using clang-format"
      COMMAND "${CMAKE_WINDOWS_BINARY_DIR}\\clang-format-windows.bat" 1)

    add_custom_target(format-check
      COMMENT "Verify source files formatting using clang-format"
      COMMAND "${CMAKE_WINDOWS_BINARY_DIR}\\clang-format-windows.bat" 2)
  else()
    add_custom_target(format
      COMMENT "Format source files using clang-format"
      COMMAND ${CLANG_FORMAT_EXE} -i -fallback-style=none -style=file ${CLANG_FORMAT_ALL_SOURCE_FILES})

    add_custom_target(format-check
      COMMENT "Verify source files formatting using clang-format"
      COMMAND ! ${CLANG_FORMAT_EXE} -output-replacements-xml -fallback-style=none -style=file ${CLANG_FORMAT_ALL_SOURCE_FILES} | tee replacements.xml | grep -q "replacement offset")
  endif()
else()
  message(STATUS "Unable to find clang-format. Not creating format targets.")
endif()
