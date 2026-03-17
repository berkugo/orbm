ACE_ROOT ?= /home/buguroglu/ACE_wrappers
TAO_ROOT ?= $(ACE_ROOT)/TAO

CXX      = g++
TAO_IDL  = $(TAO_ROOT)/TAO_IDL/tao_idl
IDL_DIR  = idl

CXXFLAGS = -fpermissive -O2 -ggdb -pipe \
           -I$(ACE_ROOT) -I$(TAO_ROOT) -I$(TAO_ROOT)/orbsvcs -I. \
           -DACE_HAS_EXCEPTIONS -D__ACE_INLINE__

LDFLAGS  = -L$(ACE_ROOT)/lib \
           -lTAO_CosNaming -lTAO_AnyTypeCode -lTAO_PortableServer \
           -lTAO -lACE -ldl -lpthread -lrt

# Tüm IDL dosyaları; AppExtended.idl #include "CommonTypedefs.idl" kullanır -> CommonTypedefs önce derlenmeli
IDL_NAMES = Hello Echo Math Stream Service CommonTypedefs AppExtended
IDL_FILES = $(addprefix $(IDL_DIR)/,$(addsuffix .idl,$(IDL_NAMES)))

# Üretilen stub/skeleton dosyaları (her IDL için)
HELLO_GEN = HelloC.cpp HelloS.cpp HelloC.h HelloS.h HelloC.inl HelloS.inl
HELLO_GEN += HelloS_T.cpp HelloS_T.h HelloS_T.inl
# Diğer arayüzler sadece generate edilir; server/client şu an sadece Hello kullanıyor
GEN_C_CPP = $(addsuffix C.cpp,$(IDL_NAMES))
GEN_S_CPP = $(addsuffix S.cpp,$(IDL_NAMES))
GEN_S_T_CPP = $(addsuffix S_T.cpp,$(IDL_NAMES))

.PHONY: all clean generate run_ns run_server run_client

all: generate server client

# IDL -> stub/skeleton: her IDL için tao_idl (-I idl ile include'lar çözülür)
$(GEN_C_CPP): %C.cpp: $(IDL_DIR)/%.idl
	$(TAO_IDL) -I$(TAO_ROOT) -I$(IDL_DIR) $(IDL_DIR)/$*.idl

# AppExtended, CommonTypedefs.idl include ettiği için CommonTypedefsC.h önce üretilmeli
AppExtendedC.cpp AppExtendedS.cpp: CommonTypedefsC.cpp

# tao_idl aynı anda hem *C.cpp hem *S.cpp üretir; *S.cpp için de rule vermezsek clean sonrası build kırılır
$(GEN_S_CPP): %S.cpp: $(IDL_DIR)/%.idl
	$(TAO_IDL) -I$(TAO_ROOT) -I$(IDL_DIR) $(IDL_DIR)/$*.idl


# Server: tüm arayüzler implemente -> tüm *C.cpp + *S.cpp
server: server.cpp $(GEN_C_CPP) $(GEN_S_CPP)
	$(CXX) $(CXXFLAGS) -o server server.cpp $(GEN_C_CPP) $(GEN_S_CPP) $(LDFLAGS)

# Client: stub'lar + callback için AppExtended skeleton
client: client.cpp $(GEN_C_CPP) AppExtendedS.cpp
	$(CXX) $(CXXFLAGS) -o client client.cpp $(GEN_C_CPP) AppExtendedS.cpp $(LDFLAGS)

# Tüm IDL'leri üret
generate: $(GEN_C_CPP) $(GEN_S_CPP)

# Helper targets
run_ns:
	@echo "Starting Naming Service..."
	LD_LIBRARY_PATH=$(ACE_ROOT)/lib:$$LD_LIBRARY_PATH \
	$(TAO_ROOT)/orbsvcs/Naming_Service/Naming_Service -ORBendpoint iiop://localhost:4500 -o ns.ior -m 0 &
	@sleep 2
	@echo "Naming Service running. IOR written to ns.ior"

run_server:
	@echo "Starting server..."
	LD_LIBRARY_PATH=$(ACE_ROOT)/lib:$$LD_LIBRARY_PATH \
	./server -ORBInitRef NameService=corbaloc:iiop:localhost:4500/NameService

run_client:
	@echo "Starting client..."
	LD_LIBRARY_PATH=$(ACE_ROOT)/lib:$$LD_LIBRARY_PATH \
	./client -ORBInitRef NameService=corbaloc:iiop:localhost:4500/NameService

clean:
	rm -f server client ns.ior
	$(foreach n,$(IDL_NAMES),rm -f $(n)C.cpp $(n)C.h $(n)C.inl $(n)S.cpp $(n)S.h $(n)S.inl $(n)S_T.cpp $(n)S_T.h $(n)S_T.inl;)
