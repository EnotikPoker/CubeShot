#include "network/Directory.h"
#include <httplib.h>
#include <openssl/pem.h>
#include <openssl/x509v3.h>
#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
using namespace cubeshot::network;
void expect(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
int main() {
    try {
        // Generate an untrusted certificate in memory; no keys or trust-store modifications on disk.
        std::unique_ptr<EVP_PKEY,decltype(&EVP_PKEY_free)> key(EVP_PKEY_Q_keygen(nullptr,nullptr,"RSA",2048),EVP_PKEY_free);
        std::unique_ptr<X509,decltype(&X509_free)> cert(X509_new(),X509_free);
        expect(key && cert,"Create TLS fixture key and certificate");
        X509_set_version(cert.get(),2); ASN1_INTEGER_set(X509_get_serialNumber(cert.get()),1);
        X509_gmtime_adj(X509_getm_notBefore(cert.get()),-60); X509_gmtime_adj(X509_getm_notAfter(cert.get()),3600);
        X509_set_pubkey(cert.get(),key.get()); auto* name=X509_get_subject_name(cert.get());
        X509_NAME_add_entry_by_txt(name,"CN",MBSTRING_ASC,reinterpret_cast<const unsigned char*>("untrusted.invalid"),-1,-1,0);
        X509_set_issuer_name(cert.get(),name); expect(X509_sign(cert.get(),key.get(),EVP_sha256())>0,"Sign test certificate");
        std::unique_ptr<BIO,decltype(&BIO_free)> certBio(BIO_new(BIO_s_mem()),BIO_free),keyBio(BIO_new(BIO_s_mem()),BIO_free);
        PEM_write_bio_X509(certBio.get(),cert.get()); PEM_write_bio_PrivateKey(keyBio.get(),key.get(),nullptr,nullptr,0,nullptr,nullptr);
        char *certData=nullptr,*keyData=nullptr;
        const auto certLength=BIO_get_mem_data(certBio.get(),&certData),keyLength=BIO_get_mem_data(keyBio.get(),&keyData);
        httplib::SSLServer::PemMemory memory{certData,static_cast<size_t>(certLength),keyData,static_cast<size_t>(keyLength),nullptr,0,nullptr};
        httplib::SSLServer server(memory); expect(server.is_valid(),"Valid local TLS server");
        server.Get("/v1/servers",[](const httplib::Request&,httplib::Response& reply){reply.set_content("{\"protocol\":3,\"servers\":[]}","application/json");});
        const auto port=server.bind_to_any_port("127.0.0.1"); expect(port>0,"Bind TLS fixture");
        std::thread thread([&]{server.listen_after_bind();});
        Directory directory; directory.list("https://127.0.0.1:"+std::to_string(port));
        std::optional<DirectoryResult> result;
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while (!result && std::chrono::steady_clock::now()<deadline) { result=directory.poll(); std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
        server.stop(); thread.join();
        expect(result && !result->error.empty(),"HTTPS with an untrusted or mismatched certificate is rejected");
        std::cout<<"Untrusted HTTPS certificate rejected\n";
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
