#include "CommonTypesC.h"
#include "ExceptionsC.h"
#include "MathServiceC.h"
#include "StoreServiceC.h"
#include "AnyServiceC.h"
#include "BigPayloadC.h"
#include "orbsvcs/CosNamingC.h"

#include <fstream>
#include <iostream>
#include <vector>
#include <cstring>

namespace {

void fill_vec(Demo::LongVec v, long base) {
    for (int i = 0; i < 16; ++i) v[i] = static_cast<CORBA::Long>(base + i);
}

Demo::Record make_record(const char* id, CORBA::ULong ver, const char* tag) {
    Demo::Record r;
    r.key_value.id = CORBA::string_dup(id);
    r.key_value.version = ver;
    r.meta_value.tag = CORBA::string_dup(tag);
    r.meta_value.note = CORBA::wstring_dup(L"hello-ws");
    r.meta_value.color_value = Demo::GREEN;
    // Deep nested struct chain values (L1 -> L2 -> L3 -> L4).
    r.deep_value.lvl2.lvl3.lvl4.s = CORBA::string_dup(id);
    r.deep_value.lvl2.lvl3.lvl4.n = 4242;
    r.deep_value.lvl2.a = CORBA::string_dup(tag);
    r.deep_value.lvl2.lvl3.w = CORBA::wstring_dup(L"deep-w");
    r.deep_value.b = CORBA::wstring_dup(L"deep-b");
    r.samples.length(4);
    r.samples[0] = 10; r.samples[1] = 20; r.samples[2] = 30; r.samples[3] = 40;
    r.payload.length(8);
    for (CORBA::ULong i = 0; i < r.payload.length(); ++i) r.payload[i] = static_cast<CORBA::Octet>(0xA0 + i);
    return r;
}

CORBA::Any make_any_struct() {
    Demo::Key k;
    k.id = CORBA::string_dup("any-key");
    k.version = 7;
    CORBA::Any a;
    a <<= k;
    return a;
}

CORBA::Any make_any_union() {
    Demo::Variant v;
    v.asLong(123);
    CORBA::Any a;
    a <<= v;
    return a;
}

CORBA::Any make_any_sequence() {
    Demo::LongSeq xs;
    xs.length(3);
    xs[0] = 1; xs[1] = 2; xs[2] = 3;
    CORBA::Any a;
    a <<= xs;
    return a;
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
            std::cout << "[Client] Resolved NameService via ORBInitRef/initial refs\n";
            return obj;
        }
    } catch (const CORBA::Exception&) {
    }
    return object_from_ref(orb, ref);
}

} // namespace

int main(int argc, char* argv[]) {
    const char* key = nullptr;
    const char* ns_ref = "corbaloc:iiop:127.0.0.1:2809/NameService";
    bool shutdown_server = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-k" && i + 1 < argc) {
            key = argv[++i];
        } else if (arg == "-n" && i + 1 < argc) {
            ns_ref = argv[++i];
        } else if (arg == "-x") {
            shutdown_server = true;
        }
    }

    try {
        CORBA::ORB_var orb = CORBA::ORB_init(argc, argv);
        CORBA::Object_var obj;
        if (key && *key) {
            std::cout << "[Client] Resolving DemoRoot via key: " << key << "\n";
            obj = object_from_ref(orb.in(), key);
        } else {
            std::cout << "[Client] Resolving NameService via " << ns_ref << "\n";
            CORBA::Object_var ns_obj = resolve_name_service(orb.in(), ns_ref);
            CosNaming::NamingContext_var root_ctx = CosNaming::NamingContext::_narrow(ns_obj.in());
            if (CORBA::is_nil(root_ctx.in())) {
                std::cerr << "[Client] Failed to narrow NameService from " << ns_ref << "\n";
                return 2;
            }
            CosNaming::Name n(1);
            n.length(1);
            n[0].id = CORBA::string_dup("DemoRoot");
            n[0].kind = CORBA::string_dup("");
            obj = root_ctx->resolve(n);
        }
        Demo::DemoRoot_var root = Demo::DemoRoot::_narrow(obj.in());
        if (CORBA::is_nil(root.in())) {
            std::cerr << "[Client] Failed to narrow DemoRoot\n";
            return 2;
        }

        CORBA::Object_var math_obj = root->get_math();
        CORBA::Object_var store_obj = root->get_store();
        CORBA::Object_var any_obj = root->get_any();
        CORBA::Object_var big_obj = root->get_big();

        Demo::MathService_var math = Demo::MathService::_narrow(math_obj.in());
        Demo::StoreService_var store = Demo::StoreService::_narrow(store_obj.in());
        Demo::AnyService_var any = Demo::AnyService::_narrow(any_obj.in());
        Demo::BigPayload_var big = Demo::BigPayload::_narrow(big_obj.in());

        // ── MathService ──
        std::cout << "[Client] add(7,5) = " << math->add(7, 5) << "\n";
        Demo::LongVec va;
        Demo::LongVec vb;
        fill_vec(va, 1);
        fill_vec(vb, 2);
        std::cout << "[Client] dot(vec,vec) = " << math->dot(va, vb) << "\n";

        Demo::LongSeq xs;
        xs.length(5);
        for (CORBA::ULong i = 0; i < xs.length(); ++i) xs[i] = static_cast<CORBA::Long>(i + 1);
        Demo::LongSeq_var scaled = math->scale(xs, 3);
        std::cout << "[Client] scale(len=" << xs.length() << ",3) -> len=" << scaled->length() << "\n";

        try {
            Demo::Record bad = make_record("", 1, "t1");
            (void)math->sum_samples(bad);
        } catch (const Demo::ValidationError& ex) {
            std::cout << "[Client] sum_samples raised ValidationError(field=" << ex.field.in()
                      << ", msg=" << ex.message.in() << ")\n";
        }

        // ── StoreService ──
        Demo::Record good = make_record("r1", 1, "t1");
        store->put(good);

        Demo::Key k;
        k.id = CORBA::string_dup("r1");
        k.version = 1;
        Demo::Record_var got = store->get(k);
        std::cout << "[Client] get(r1,1) payload_len=" << got->payload.length() << "\n";

        try {
            Demo::Key missing;
            missing.id = CORBA::string_dup("missing");
            missing.version = 1;
            (void)store->get(missing);
        } catch (const Demo::NotFound& ex) {
            std::cout << "[Client] get(missing) raised NotFound(resource=" << ex.resource.in()
                      << ", id=" << ex.id.in() << ")\n";
        }

        Demo::RecordList_var list = store->list_by_tag("t1");
        std::cout << "[Client] list_by_tag(t1) len=" << list->length() << "\n";

        Demo::Variant v;
        v.asDouble(1.25);
        Demo::Variant_var v2 = store->normalize(v);
        std::cout << "[Client] normalize(Variant disc=" << v2->_d() << ")\n";

        Demo::BoolVariant bv;
        bv.t(CORBA::string_dup("true-branch"));
        Demo::BoolVariant_var bv2 = store->flip(bv);
        std::cout << "[Client] flip(BoolVariant disc=" << (bv2->_d() ? "TRUE" : "FALSE") << ")\n";

        Demo::ColorVariant cv;
        cv.r(10);
        Demo::ColorVariant_var cv2 = store->tint(cv);
        std::cout << "[Client] tint(ColorVariant disc=" << cv2->_d() << ")\n";

        // ── AnyService ──
        CORBA::Any a1 = make_any_struct();
        CORBA::Any_var a1r = any->echo_any(a1);
        (void)a1r; // just to carry bytes on wire

        CORBA::Any a2 = make_any_union();
        CORBA::Any_var nested_any = any->echo_nested_any(a2);
        (void)nested_any;

        // any that contains a sequence<long>
        CORBA::Any seq_any = make_any_sequence();
        CORBA::Any_var mapped = any->map_any(seq_any);
        (void)mapped;

        CORBA::String_var desc = any->describe_any(make_any_sequence(), "seq");
        std::cout << "[Client] describe_any -> " << desc.in() << "\n";

        // ── BigPayload (fragment trigger) ──
        try {
            Demo::Octets_var blob = big->get_blob(1024u * 1024u); // 1MiB
            std::cout << "[Client] get_blob(1MiB) len=" << blob->length() << "\n";
        } catch (const Demo::ValidationError& ex) {
            std::cout << "[Client] get_blob raised ValidationError(field=" << ex.field.in()
                      << ", msg=" << ex.message.in() << ")\n";
        }

        if (shutdown_server) {
            std::cout << "[Client] shutdown requested; stopping server\n";
            root->shutdown();
        } else {
            std::cout << "[Client] leaving server running (pass -x to shutdown)\n";
        }

        orb->destroy();
        return 0;
    } catch (const CORBA::Exception& ex) {
        ex._tao_print_exception("Client exception");
        return 1;
    }
}

