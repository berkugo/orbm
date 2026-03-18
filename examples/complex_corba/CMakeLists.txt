cmake_minimum_required(VERSION 3.14)
project(complex_corba_demo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_program(TAO_IDL tao_idl)
if (NOT TAO_IDL)
  message(FATAL_ERROR "tao_idl not found. Install ACE/TAO and ensure tao_idl is in PATH.")
endif()

get_filename_component(TAO_IDL_REALPATH "${TAO_IDL}" REALPATH)
get_filename_component(_tao_idl_bindir "${TAO_IDL_REALPATH}" DIRECTORY)
get_filename_component(_tao_root_from_idl "${_tao_idl_bindir}" DIRECTORY)
get_filename_component(_ace_root_from_tao "${_tao_root_from_idl}" DIRECTORY)

set(ACE_ROOT_DIR $ENV{ACE_ROOT})
if (NOT ACE_ROOT_DIR)
  set(ACE_ROOT_DIR "${_ace_root_from_tao}")
endif()

set(TAO_ROOT_DIR $ENV{TAO_ROOT})
if (NOT TAO_ROOT_DIR)
  set(TAO_ROOT_DIR "${ACE_ROOT_DIR}/TAO")
endif()

if (NOT EXISTS "${ACE_ROOT_DIR}/ace/pre.h")
  # Cursor sandbox environment default (used elsewhere in this repo).
  if (EXISTS "/root/project_x/corba_Viewer/ACE_wrappers/ace/pre.h")
    set(ACE_ROOT_DIR "/root/project_x/corba_Viewer/ACE_wrappers")
    if (NOT TAO_ROOT_DIR OR NOT EXISTS "${TAO_ROOT_DIR}/tao/corba.h")
      set(TAO_ROOT_DIR "/root/project_x/corba_Viewer/ACE_wrappers/TAO")
    endif()
  endif()
endif()

if (NOT EXISTS "${ACE_ROOT_DIR}/ace/pre.h")
  message(FATAL_ERROR "ACE headers not found (missing ${ACE_ROOT_DIR}/ace/pre.h). Set ACE_ROOT to your ACE_wrappers directory.")
endif()

if (NOT EXISTS "${TAO_ROOT_DIR}/tao/corba.h")
  message(FATAL_ERROR "TAO headers not found (missing ${TAO_ROOT_DIR}/tao/corba.h). Set TAO_ROOT (usually $ACE_ROOT/TAO).")
endif()

set(ACE_LIB "${ACE_ROOT_DIR}/lib/libACE.so")
set(TAO_LIB "${ACE_ROOT_DIR}/lib/libTAO.so")
set(TAO_PORTABLESERVER_LIB "${ACE_ROOT_DIR}/lib/libTAO_PortableServer.so")
set(TAO_ANYTYPECODE_LIB "${ACE_ROOT_DIR}/lib/libTAO_AnyTypeCode.so")
set(TAO_IORTABLE_LIB "${ACE_ROOT_DIR}/lib/libTAO_IORTable.so")
set(TAO_COSNAMING_LIB "${ACE_ROOT_DIR}/lib/libTAO_CosNaming.so")

if (NOT EXISTS "${ACE_LIB}" OR NOT EXISTS "${TAO_LIB}")
  message(FATAL_ERROR "ACE/TAO libraries not found. Ensure $ACE_ROOT/lib is available and contains libACE/libTAO.")
endif()
if (NOT EXISTS "${TAO_COSNAMING_LIB}")
  message(FATAL_ERROR "TAO CosNaming library not found (missing ${TAO_COSNAMING_LIB}).")
endif()

set(IDL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/idl)
set(GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/gen)
file(MAKE_DIRECTORY ${GEN_DIR})

set(IDL_FILES
  ${IDL_DIR}/CommonTypes.idl
  ${IDL_DIR}/Exceptions.idl
  ${IDL_DIR}/MathService.idl
  ${IDL_DIR}/StoreService.idl
  ${IDL_DIR}/AnyService.idl
  ${IDL_DIR}/BigPayload.idl
)

set(GEN_SERVER_SOURCES)
set(GEN_CLIENT_SOURCES)
foreach(idl ${IDL_FILES})
  get_filename_component(base ${idl} NAME_WE)

  add_custom_command(
    OUTPUT
      ${GEN_DIR}/${base}C.h
      ${GEN_DIR}/${base}C.cpp
      ${GEN_DIR}/${base}S.h
      ${GEN_DIR}/${base}S.cpp
    COMMAND ${TAO_IDL}
      -Wb,pre_include=ace/pre.h
      -Wb,post_include=ace/post.h
      -o ${GEN_DIR}
      -I ${IDL_DIR}
      ${idl}
    DEPENDS ${idl}
    COMMENT "Generating TAO stubs for ${base}.idl"
    VERBATIM
  )

  list(APPEND GEN_CLIENT_SOURCES
    ${GEN_DIR}/${base}C.cpp
  )
  list(APPEND GEN_SERVER_SOURCES
    ${GEN_DIR}/${base}C.cpp
    ${GEN_DIR}/${base}S.cpp
  )
endforeach()

add_executable(complex_corba_server
  server/main.cpp
  ${GEN_SERVER_SOURCES}
)

add_executable(complex_corba_client
  client/main.cpp
  ${GEN_CLIENT_SOURCES}
)

target_include_directories(complex_corba_server PRIVATE ${GEN_DIR} ${IDL_DIR})
target_include_directories(complex_corba_client PRIVATE ${GEN_DIR} ${IDL_DIR})

target_include_directories(complex_corba_server PRIVATE
  "${ACE_ROOT_DIR}"
  "${TAO_ROOT_DIR}"
  "${TAO_ROOT_DIR}/orbsvcs"
)
target_include_directories(complex_corba_client PRIVATE
  "${ACE_ROOT_DIR}"
  "${TAO_ROOT_DIR}"
  "${TAO_ROOT_DIR}/orbsvcs"
)

target_compile_definitions(complex_corba_server PRIVATE ACE_HAS_EXCEPTIONS=1 __ACE_INLINE__=1)
target_compile_definitions(complex_corba_client PRIVATE ACE_HAS_EXCEPTIONS=1 __ACE_INLINE__=1)

target_link_libraries(complex_corba_server PRIVATE
  ${TAO_PORTABLESERVER_LIB}
  ${TAO_ANYTYPECODE_LIB}
  ${TAO_IORTABLE_LIB}
  ${TAO_COSNAMING_LIB}
  ${TAO_LIB}
  ${ACE_LIB}
)
target_link_libraries(complex_corba_client PRIVATE
  ${TAO_ANYTYPECODE_LIB}
  ${TAO_COSNAMING_LIB}
  ${TAO_LIB}
  ${ACE_LIB}
)

