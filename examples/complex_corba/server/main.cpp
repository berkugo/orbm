#include "CommonTypesS.h"
#include "ExceptionsS.h"
#include "MathServiceS.h"
#include "StoreServiceS.h"
#include "AnyServiceS.h"
#include "BigPayloadS.h"

#include "tao/IORTable/IORTable.h"
#include "orbsvcs/CosNamingC.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>
#include <cstring>

namespace {

void bind_name(CosNaming::NamingContext_ptr ctx, const char* name, CORBA::Object_ptr obj) {
    CosNaming::Name n(1);
    n.length(1);
    n[0].id = CORBA::string_dup(name);
    n[0].kind = CORBA::string_dup("");
    ctx->rebind(n, obj);
    std::cout << "[Server] Naming bind: " << name << std::endl;
}

CORBA::Object_var object_from_ref(CORBA::ORB_ptr orb, const char* ref) {
    std::string s = ref ? ref : "";
    if (s.rfind("file://", 0) == 0) {
        std::ifstream in(s.substr(7));
        std::string ior;
        std::getline(in, ior);
        return orb->string_to_object(ior.c_str());
    }
    return orb->string_to_object(s.c_str());
}

CORBA::Object_var resolve_name_service(CORBA::ORB_ptr orb, const char* ref) {
    try {
        CORBA::Object_var obj = orb->resolve_initial_references("NameService");
        if (!CORBA::is_nil(obj.in())) {
            std::cout << "[Server] Resolved NameService via ORBInitRef/initial refs\n";
            return obj;
        }
    } catch (const CORBA::Exception&) {
    }
    return object_from_ref(orb, ref);
}

Demo::Record make_record(const char* id, CORBA::ULong ver, const char* tag, Demo::Color c, CORBA::ULong payload_len) {
    Demo::Record r;
    r.key_value.id = CORBA::string_dup(id);
    r.key_value.version = ver;
    r.meta_value.tag = CORBA::string_dup(tag);
    r.meta_value.note = CORBA::wstring_dup(L"note");
    r.meta_value.color_value = c;
    // Deep nested struct chain values (L1 -> L2 -> L3 -> L4).
    r.deep_value.lvl2.lvl3.lvl4.s = CORBA::string_dup(id);
    r.deep_value.lvl2.lvl3.lvl4.n = payload_len;
    r.deep_value.lvl2.a = CORBA::string_dup(tag);
    r.deep_value.lvl2.lvl3.w = CORBA::wstring_dup(L"deep-w");
    r.deep_value.b = CORBA::wstring_dup(L"deep-b");
    r.samples.length(5);
    for (CORBA::ULong i = 0; i < r.samples.length(); ++i) r.samples[i] = static_cast<CORBA::Long>(i + 1);
    r.payload.length(payload_len);
    for (CORBA::ULong i = 0; i < r.payload.length(); ++i) r.payload[i] = static_cast<CORBA::Octet>(i & 0xFF);
    return r;
}

class MathService_impl : public virtual POA_Demo::MathService {
public:
    CORBA::Long add(CORBA::Long a, CORBA::Long b) override {
        std::cout << "[Server] MathService::add(" << a << "," << b << ")\n";
        return a + b;
    }

    CORBA::Double dot(const Demo::LongVec a, const Demo::LongVec b) override {
        std::cout << "[Server] MathService::dot(LongVec,LongVec)\n";
        CORBA::LongLong acc = 0;
        for (int i = 0; i < 16; ++i) acc += static_cast<CORBA::LongLong>(a[i]) * static_cast<CORBA::LongLong>(b[i]);
        return static_cast<CORBA::Double>(acc);
    }

    Demo::LongSeq* scale(const Demo::LongSeq& xs, CORBA::Long factor) override {
        std::cout << "[Server] MathService::scale(seq<long>, " << factor << ") len=" << xs.length() << "\n";
        auto* out = new Demo::LongSeq();
        out->length(xs.length());
        for (CORBA::ULong i = 0; i < xs.length(); ++i) (*out)[i] = xs[i] * factor;
        return out;
    }

    CORBA::Long sum_samples(const Demo::Record& r) override {
        std::cout << "[Server] MathService::sum_samples(Record)\n";
        if (std::strlen(r.key_value.id.in()) == 0) {
            Demo::ValidationError ex;
            ex.field = CORBA::string_dup("key.id");
            ex.message = CORBA::string_dup("empty id");
            throw ex;
        }
        CORBA::Long sum = 0;
        for (CORBA::ULong i = 0; i < r.samples.length(); ++i) sum += r.samples[i];
        return sum;
    }
};

class StoreService_impl : public virtual POA_Demo::StoreService {
public:
    void put(const Demo::Record& r) override {
        std::cout << "[Server] StoreService::put(id=" << r.key_value.id.in() << ", ver=" << r.key_value.version << ")\n";
        if (r.key_value.version == 0) {
            Demo::ValidationError ex;
            ex.field = CORBA::string_dup("key.version");
            ex.message = CORBA::string_dup("version must be > 0");
            throw ex;
        }
        records_.push_back(r);
    }

    Demo::Record* get(const Demo::Key& k) override {
        std::cout << "[Server] StoreService::get(id=" << k.id.in() << ", ver=" << k.version << ")\n";
        for (const auto& r : records_) {
            if (std::strcmp(r.key_value.id.in(), k.id.in()) == 0 && r.key_value.version == k.version) {
                return new Demo::Record(r);
            }
        }
        Demo::NotFound ex;
        ex.resource = CORBA::string_dup("Record");
        ex.id = CORBA::string_dup(k.id.in());
        throw ex;
    }

    Demo::RecordList* list_by_tag(const char* tag) override {
        std::cout << "[Server] StoreService::list_by_tag(" << tag << ")\n";
        auto* out = new Demo::RecordList();
        // Keep output small but non-trivial.
        CORBA::ULong n = 0;
        for (const auto& r : records_) {
            if (std::strcmp(r.meta_value.tag.in(), tag) == 0) ++n;
        }
        out->length(n);
        CORBA::ULong idx = 0;
        for (const auto& r : records_) {
            if (std::strcmp(r.meta_value.tag.in(), tag) == 0) (*out)[idx++] = r;
        }
        return out;
    }

    Demo::Variant* normalize(const Demo::Variant& v) override {
        std::cout << "[Server] StoreService::normalize(Variant)\n";
        auto* out = new Demo::Variant();
        switch (v._d()) {
            case 0: out->asLong(v.asLong() + 1); break;
            case 1: out->asDouble(v.asDouble() * 2.0); break;
            case 2: out->asString(CORBA::string_dup(v.asString())); break;
            default: {
                const auto& blob = v.asBlob();
                out->asBlob(blob);
                break;
            }
        }
        return out;
    }

    Demo::BoolVariant* flip(const Demo::BoolVariant& v) override {
        std::cout << "[Server] StoreService::flip(BoolVariant)\n";
        auto* out = new Demo::BoolVariant();
        if (v._d()) out->f(CORBA::wstring_dup(L"flipped-false"));
        else out->t(CORBA::string_dup("flipped-true"));
        return out;
    }

    Demo::ColorVariant* tint(const Demo::ColorVariant& v) override {
        std::cout << "[Server] StoreService::tint(ColorVariant)\n";
        auto* out = new Demo::ColorVariant();
        switch (v._d()) {
            case Demo::RED: out->r(v.r() + 10); break;
            case Demo::GREEN: out->g(v.g() + 20); break;
            case Demo::BLUE: out->b(v.b() + 30); break;
            default: out->other(CORBA::string_dup("other")); break;
        }
        return out;
    }

    void seed_defaults() {
        records_.push_back(make_record("a", 1, "t1", Demo::RED, 32));
        records_.push_back(make_record("b", 1, "t1", Demo::GREEN, 64));
        records_.push_back(make_record("c", 2, "t2", Demo::BLUE, 16));
    }

private:
    std::vector<Demo::Record> records_;
};

class AnyService_impl : public virtual POA_Demo::AnyService {
public:
    CORBA::Any* echo_any(const CORBA::Any& v) override {
        std::cout << "[Server] AnyService::echo_any(any)\n";
        auto* out = new CORBA::Any(v);
        return out;
    }

    CORBA::Any* map_any(const CORBA::Any& v) override {
        std::cout << "[Server] AnyService::map_any(any)\n";
        return new CORBA::Any(v);
    }

    CORBA::Any* echo_nested_any(const CORBA::Any& v) override {
        std::cout << "[Server] AnyService::echo_nested_any(any)\n";
        CORBA::Any inner;
        inner <<= v; // tk_any
        return new CORBA::Any(inner);
    }

    char* describe_any(const CORBA::Any& v, const char* hint) override {
        std::cout << "[Server] AnyService::describe_any(any," << hint << ")\n";
        std::string s = std::string("hint=") + hint + ", tc.kind=" + std::to_string(v.type()->kind());
        return CORBA::string_dup(s.c_str());
    }
};

class BigPayload_impl : public virtual POA_Demo::BigPayload {
public:
    Demo::Octets* get_blob(CORBA::ULong size) override {
        std::cout << "[Server] BigPayload::get_blob(" << size << ")\n";
        if (size == 0 || size > (1024u * 1024u * 8u)) {
            Demo::ValidationError ex;
            ex.field = CORBA::string_dup("size");
            ex.message = CORBA::string_dup("size must be 1..8MiB");
            throw ex;
        }
        auto* out = new Demo::Octets();
        out->length(size);
        for (CORBA::ULong i = 0; i < size; ++i) (*out)[i] = static_cast<CORBA::Octet>(i & 0xFF);
        return out;
    }

    Demo::Record* get_big_record(CORBA::ULong size) override {
        std::cout << "[Server] BigPayload::get_big_record(" << size << ")\n";
        if (size == 0 || size > (1024u * 1024u * 2u)) {
            Demo::ValidationError ex;
            ex.field = CORBA::string_dup("size");
            ex.message = CORBA::string_dup("size must be 1..2MiB");
            throw ex;
        }
        auto r = make_record("big", 999, "payload", Demo::OTHER, size);
        return new Demo::Record(r);
    }
};

class DemoRoot_impl : public virtual POA_Demo::DemoRoot {
public:
    DemoRoot_impl(CORBA::ORB_ptr orb,
                  CORBA::Object_ptr math,
                  CORBA::Object_ptr store,
                  CORBA::Object_ptr any,
                  CORBA::Object_ptr big)
        : orb_(CORBA::ORB::_duplicate(orb)),
          math_(CORBA::Object::_duplicate(math)),
          store_(CORBA::Object::_duplicate(store)),
          any_(CORBA::Object::_duplicate(any)),
          big_(CORBA::Object::_duplicate(big)) {}

    CORBA::Object_ptr get_math() override { return CORBA::Object::_duplicate(math_.in()); }
    CORBA::Object_ptr get_store() override { return CORBA::Object::_duplicate(store_.in()); }
    CORBA::Object_ptr get_any() override { return CORBA::Object::_duplicate(any_.in()); }
    CORBA::Object_ptr get_big() override { return CORBA::Object::_duplicate(big_.in()); }

    void shutdown() override {
        std::cout << "[Server] DemoRoot::shutdown\n";
        orb_->shutdown(0);
    }

private:
    CORBA::ORB_var orb_;
    CORBA::Object_var math_;
    CORBA::Object_var store_;
    CORBA::Object_var any_;
    CORBA::Object_var big_;
};

} // namespace

int main(int argc, char* argv[]) {
    const char* ior_out = "server.ior";
    const char* ns_ref = "corbaloc:iiop:127.0.0.1:2809/NameService";
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-o" && i + 1 < argc) {
            ior_out = argv[++i];
        } else if (arg == "-n" && i + 1 < argc) {
            ns_ref = argv[++i];
        }
    }

    try {
        CORBA::ORB_var orb = CORBA::ORB_init(argc, argv);
        CORBA::Object_var poa_obj = orb->resolve_initial_references("RootPOA");
        PortableServer::POA_var root_poa = PortableServer::POA::_narrow(poa_obj.in());
        PortableServer::POAManager_var mgr = root_poa->the_POAManager();

        auto* math_impl = new MathService_impl();
        auto* store_impl = new StoreService_impl();
        store_impl->seed_defaults();
        auto* any_impl = new AnyService_impl();
        auto* big_impl = new BigPayload_impl();

        PortableServer::ObjectId_var oid_math = root_poa->activate_object(math_impl);
        PortableServer::ObjectId_var oid_store = root_poa->activate_object(store_impl);
        PortableServer::ObjectId_var oid_any = root_poa->activate_object(any_impl);
        PortableServer::ObjectId_var oid_big = root_poa->activate_object(big_impl);

        Demo::MathService_var math_ref = Demo::MathService::_narrow(root_poa->id_to_reference(oid_math.in()));
        Demo::StoreService_var store_ref = Demo::StoreService::_narrow(root_poa->id_to_reference(oid_store.in()));
        Demo::AnyService_var any_ref = Demo::AnyService::_narrow(root_poa->id_to_reference(oid_any.in()));
        Demo::BigPayload_var big_ref = Demo::BigPayload::_narrow(root_poa->id_to_reference(oid_big.in()));

        auto* root_impl = new DemoRoot_impl(orb.in(), math_ref.in(), store_ref.in(), any_ref.in(), big_ref.in());
        PortableServer::ObjectId_var oid_root = root_poa->activate_object(root_impl);
        CORBA::Object_var root_obj = root_poa->id_to_reference(oid_root.in());

        CORBA::String_var ior = orb->object_to_string(root_obj.in());
        std::cout << "[Server] DemoRoot IOR: " << ior.in() << "\n";

        mgr->activate();
        std::cout << "[Server] POA manager activated\n";

        // Bind objects into Naming Service for ORBM discovery.
        try {
            std::cout << "[Server] Resolving NameService via " << ns_ref << "\n";
            CORBA::Object_var ns_obj = resolve_name_service(orb.in(), ns_ref);
            if (CORBA::is_nil(ns_obj.in())) {
                std::cerr << "[Server] NameService string_to_object returned nil\n";
            } else {
                CosNaming::NamingContext_var root_ctx = CosNaming::NamingContext::_narrow(ns_obj.in());
                if (CORBA::is_nil(root_ctx.in())) {
                    std::cerr << "[Server] Failed to narrow NameService to NamingContext\n";
                } else {
                    bind_name(root_ctx.in(), "DemoRoot", root_obj.in());
                    bind_name(root_ctx.in(), "MathService", math_ref.in());
                    bind_name(root_ctx.in(), "StoreService", store_ref.in());
                    bind_name(root_ctx.in(), "AnyService", any_ref.in());
                    bind_name(root_ctx.in(), "BigPayload", big_ref.in());
                }
            }
        } catch (const CORBA::Exception& ex) {
            ex._tao_print_exception("[Server] NameService bind failed");
        }

        // Also expose a stable corbaloc endpoint via IORTable so we don't depend on hostnames in IOR strings.
        try {
            CORBA::Object_var tbl_obj = orb->resolve_initial_references("IORTable");
            IORTable::Table_var tbl = IORTable::Table::_narrow(tbl_obj.in());
            if (!CORBA::is_nil(tbl.in())) {
                tbl->bind("DemoRoot", ior.in());
                std::cout << "[Server] DemoRoot corbaloc: corbaloc:iiop:127.0.0.1:4501/DemoRoot\n";
            }
        } catch (...) {
            // Non-fatal; the file IOR will still be written.
        }

        FILE* fp = std::fopen(ior_out, "w");
        if (fp) {
            std::fputs(ior.in(), fp);
            std::fputs("\n", fp);
            std::fclose(fp);
            std::cout << "[Server] Wrote IOR to " << ior_out << "\n";
        } else {
            std::cerr << "[Server] Failed to write IOR to " << ior_out << "\n";
        }

        orb->run();

        root_poa->destroy(1, 1);
        orb->destroy();
        return 0;
    } catch (const CORBA::Exception& ex) {
        ex._tao_print_exception("Server exception");
        return 1;
    }
}

