#pragma once

#include <map>

#include "RWLockable.h"
#include "RemoteParty.h"
#include "Task.h"
#include "credb/defines.h"
#include "util/OperationType.h"
#include <bitstream.h>

namespace credb
{
namespace trusted
{

enum class PeerState
{
    Attestation_Phase1,
    Attestation_Phase2,
    Connected
};

class Enclave;

class Peer : public RemoteParty
{
public:
    Peer(Enclave &enclave, remote_party_id id, const std::string &hostname, uint16_t port, bool is_initiator);
    Peer(const Peer &other) = delete;

    bitstream *receive_response(operation_id_t op_id, bool wait = true);

    void handle_message(const uint8_t *data, uint32_t len) override;

    bool get_encryption_key(sgx_ec_key_128bit_t **key) override;

    bool is_initiator() const { return m_is_initiator; }

    PeerType get_peer_type() const { return m_peer_type; }

    void set_peer_type(PeerType peer_type);

    const std::string &hostname() const { return m_hostname; }

    uint16_t port() const { return m_port; }

    operation_id_t call(const std::string &collection,
                        taskid_t task_id,
                        const std::string &key,
                        const std::string &path,
                        const std::vector<std::string> &args);

    operation_id_t get_next_operation_id();

    bool connected() const override { return m_state == PeerState::Connected; }

    void insert_response(operation_id_t op_id, const uint8_t *data, uint32_t length);

private:
    void handle_op_response(bitstream &input);
    void handle_op_forwarded_request(bitstream &input);

    void set_connected();

    sgx_status_t handle_attestation_message_one(bitstream &input);
    sgx_status_t handle_attestation_message_three(bitstream &input);
    void handle_attestation_result(bitstream &input);

    PeerState m_state = PeerState::Attestation_Phase1;

    const bool m_is_initiator;
    PeerType m_peer_type;

    uint32_t m_group_id;

    operation_id_t m_next_operation_id = 1;

#ifndef FAKE_ENCLAVE
    // Diffie Hellman Exchange stuff
    sgx_ec256_public_t m_dhke_other_public; // other's public key
    sgx_ec256_public_t m_dhke_public; // my public key
    sgx_ec256_private_t m_dhke_private; // my private key

    sgx_ec_key_128bit_t m_vk_key; // Shared secret key for the REPORT_DATA
    sgx_ec_key_128bit_t m_mk_key; // Shared secret key for generating MAC's
    sgx_ec_key_128bit_t m_smk_key; // Used only for SIGMA protocol
    sgx_ec_key_128bit_t m_sk_internal_key;
    sgx_ps_sec_prop_desc_t m_ps_sec_prop;

    sgx_ec_key_128bit_t m_encryption_key;
#endif

    const std::string m_hostname;
    const uint16_t m_port;

    std::map<operation_id_t, bitstream *> m_responses;
};

inline operation_id_t Peer::get_next_operation_id()
{
    auto ret = m_next_operation_id;
    m_next_operation_id++;
    return ret;
}

} // namespace trusted
} // namespace credb
