#include "op_info.h"

#include "Transaction.h"
#include "Ledger.h"

namespace credb
{
namespace trusted
{

void has_obj_info_t::read_from_req(bitstream &req)
{
    req >> collection >> key >> result;
    sid = transaction().ledger.get_shard(collection, key);
}

void has_obj_info_t::collect_shard_lock_type()
{
    transaction().set_read_lock_if_not_present(sid);
}

bool has_obj_info_t::validate_read()
{
    ObjectEventHandle obj;

    {
        auto res = transaction().ledger.has_object(collection, key);
        
        if(res != result)
        {
            transaction().error = "Key [" + key + "] reads outdated value";
            return false;
        }
    }

    if(transaction().generate_witness)
    {
        auto &writer = transaction().writer;

        writer.start_map("");
        writer.write_string("type", "HasObject");
        writer.write_string("key", key);
        writer.write_boolean("result", result);
        writer.end_map();
    }

    return true;
}

get_info_t::get_info_t(Transaction &tx, bitstream &req)
        : read_op_t(tx)
{
    req >> m_collection >> m_key >> m_eid;
    m_sid = transaction().ledger.get_shard(m_collection, m_key);
}

get_info_t::get_info_t(Transaction &tx, const std::string &collection, const std::string &key, const event_id_t eid)
        : read_op_t(tx), m_collection(collection), m_key(key), m_eid(eid),
           m_sid(transaction().ledger.get_shard(collection, key))
{}

void get_info_t::collect_shard_lock_type()
{
    transaction().set_read_lock_if_not_present(m_sid);
}

bool get_info_t::validate_read()
{
    ObjectEventHandle obj;
    if(transaction().isolation_level() != IsolationLevel::ReadCommitted)
    {
        if(!transaction().check_repeatable_read(obj, m_collection, m_key, m_sid, m_eid))
        {
            return false;
        }
    }
    else
    {
        const LockType lock_type = transaction().shards_lock_type[m_sid];
        event_id_t latest_eid;
        
        bool res = transaction().ledger.get_latest_version(obj, transaction().op_context, m_collection, m_key, "",
                                                 latest_eid, transaction().lock_handle, lock_type);

        if(!res)
        {
            return false;
        }
    }

    if(transaction().generate_witness)
    {
        auto &writer = transaction().writer;

        writer.start_map();
        writer.write_string("type", "GetObject");
        writer.write_string("key", m_key);
        writer.write_integer("shard", m_eid.shard);
        writer.write_integer("block", m_eid.block);
        writer.write_integer("index", m_eid.index);
        writer.write_document("content", obj.value());
        writer.end_map();
    }
    return true;
}

put_info_t::put_info_t(Transaction &tx, bitstream &req)
    : write_op_t(tx)
{
    req >> m_collection >> m_key >> m_doc;
    m_sid = transaction().ledger.get_shard(m_collection, m_key);
}

put_info_t::put_info_t(Transaction &tx, const std::string &collection, const std::string &key, const json::Document &doc)
    : write_op_t(tx), m_collection(collection), m_key(key), m_doc(doc.duplicate())
{
    m_sid = transaction().ledger.get_shard(m_collection, m_key);
}

void put_info_t::collect_shard_lock_type()
{
    transaction().shards_lock_type[m_sid] = LockType::Write;
}

bool put_info_t::do_write()
{
    const event_id_t new_eid = transaction().ledger.put(transaction().op_context, m_collection, m_key, m_doc, "", &transaction().lock_handle);

    if(new_eid && transaction().generate_witness)
    {
        auto &writer = transaction().writer;

        writer.start_map("");
        writer.write_string("type", "PutObject");
        writer.write_string("collection", m_collection);
        writer.write_string("key", m_key);
        writer.write_integer("shard", new_eid.shard);
        writer.write_integer("block", new_eid.block);
        writer.write_integer("index", new_eid.index);
        writer.write_document("content", m_doc);
        writer.end_map();
    }

    return true;
}

add_info_t::add_info_t(Transaction &tx, bitstream &req)
    : write_op_t(tx)
{
    req >> m_collection >> m_key >> m_doc;
    m_sid = transaction().ledger.get_shard(m_collection, m_key);
}

add_info_t::add_info_t(Transaction &tx, const std::string &collection, const std::string &key, const json::Document &doc)
    : write_op_t(tx), m_collection(collection), m_key(key), m_doc(doc.duplicate())
{
    m_sid = transaction().ledger.get_shard(m_collection, m_key);
}

void add_info_t::collect_shard_lock_type()
{
    transaction().shards_lock_type[m_sid] = LockType::Write;
}

bool add_info_t::do_write()
{
    const event_id_t new_eid = transaction().ledger.add(transaction().op_context, m_collection, m_key, m_doc, "", &transaction().lock_handle);

    if(transaction().generate_witness)
    {
        auto &writer = transaction().writer;

        writer.start_map("");
        writer.write_string("type", "AddObject");
        writer.write_string("key", m_key);
        writer.write_integer("shard", new_eid.shard);
        writer.write_integer("block", new_eid.block);
        writer.write_integer("index", new_eid.index);
        writer.write_document("content", m_doc);
        writer.end_map();
    }

    return true;
}

void remove_info_t::read_from_req(bitstream &req)
{
    req >> collection >> key;
    sid = transaction().ledger.get_shard(collection, key);
}

void remove_info_t::collect_shard_lock_type()
{
    transaction().shards_lock_type[sid] = LockType::Write;
}

bool remove_info_t::do_write()
{
    const event_id_t new_eid = transaction().ledger.remove(transaction().op_context, collection, key, &transaction().lock_handle);
    if(transaction().generate_witness)
    {
        auto &writer = transaction().writer;

        writer.start_map("");
        writer.write_string("type", "RemoveObject");
        writer.write_string("key", key);
        writer.write_integer("shard", new_eid.shard);
        writer.write_integer("block", new_eid.block);
        writer.write_integer("index", new_eid.index);
        writer.end_map();
    }
    return true;
}

void find_info_t::read_from_req(bitstream &req)
{
    req >> collection >> predicates >> projection >> limit;
    uint32_t size = 0;
    req >> size;
    for(uint32_t i = 0; i < size; ++i)
    {
        std::string key;
        event_id_t eid;
        req >> key >> eid;
        shard_id_t sid = transaction().ledger.get_shard(collection, key);
        res.emplace_back(key, sid, eid);
    }
}

void find_info_t::collect_shard_lock_type()
{
    for(const auto &it : res)
    {
        transaction().set_read_lock_if_not_present(std::get<1>(it));
    }
}

void find_info_t::write_witness(
                   const std::string &key,
                   const event_id_t &eid,
                   const json::Document &value)
{
    auto &writer = transaction().writer;

    writer.start_map("");
    writer.write_string("key", key);
    writer.write_integer("shard", eid.shard);
    writer.write_integer("block", eid.block);
    writer.write_integer("index", eid.index);
    writer.write_document("content", value);
    writer.end_map();
}

bool find_info_t::validate_no_dirty_read()
{
    for(const auto & [key, sid, eid] : res)
    {
        const LockType lock_type = transaction().shards_lock_type[sid];
        event_id_t latest_eid;
        ObjectEventHandle obj;
        bool res = transaction().ledger.get_latest_version(obj, transaction().op_context, collection, key, "",
                                                 latest_eid, transaction().lock_handle, lock_type);
        if(!res)
        {
            transaction().error = "Key [" + key + "] reads outdated value";
            return false;
        }

        if(transaction().generate_witness)
        {
            write_witness(key, eid, obj.value());
        }
    }
    return true;
}

bool find_info_t::validate_repeatable_read()
{
    for(const auto & [key, sid, eid] : res)
    {
        ObjectEventHandle obj;
        if(!transaction().check_repeatable_read(obj, collection, key, sid, eid))
        {
            return false;
        }

        if(transaction().generate_witness)
        {
            write_witness(key, eid, obj.value());
        }
    }
    return true;
}

bool find_info_t::validate_no_phantom()
{
    // build the set of known result
    std::unordered_set<event_id_t> eids;
    for(const auto &it : res)
    {
        eids.emplace(std::get<2>(it));
    }

    // find again and check if there is phantom read
    auto it = transaction().ledger.find(transaction().op_context, collection, predicates, &transaction().lock_handle);
    std::string key;
    ObjectEventHandle hdl;

    for(auto eid = it.next(key, hdl); hdl.valid(); eid = it.next(key, hdl))
    {
        auto cnt = eids.erase(eid);
        if(!cnt)
        {
            transaction().error = "Phantom read: key=" + key;
            return false;
        }

        if(transaction().generate_witness)
        {
            json::Document value = hdl.value();
            if(!projection.empty())
            {
                json::Document filtered(value, projection);
                write_witness(key, eid, filtered);
            }
            else
            {
                write_witness(key, eid, value);
            }
        }
    }

    if(!eids.empty())
    {
        transaction().error = "Phantom read: too few results";
        return false;
    }

    return true;
}

bool find_info_t::validate_read()
{
    if(transaction().generate_witness)
    {
        auto &writer = transaction().writer;

        writer.start_map("");
        writer.write_string("type", "FindObjects");
        writer.write_string("collection", collection);
        writer.write_document("predicates", predicates);
        writer.start_array("projection");
        for(const auto &proj : projection)
        {
            writer.write_string("", proj);
        }
        writer.end_array();
        writer.write_integer("limit", limit);
        writer.start_array("results");
    }

    bool ok = false;

    switch(transaction().isolation_level())
    {
    case IsolationLevel::ReadCommitted:
        ok = validate_no_dirty_read();
        break;
    case IsolationLevel::RepeatableRead:
        ok = validate_repeatable_read();
        break;
    case IsolationLevel::Serializable:
        ok = validate_no_phantom();
        break;
    default:
        transaction().error = "Unknown IsolationLevel " + std::to_string(static_cast<uint8_t>(transaction().isolation_level()));
        return false;
    }

    if(!ok)
    {
        return false;
    }

    if(transaction().generate_witness)
    {
        transaction().writer.end_array();
        transaction().writer.end_map();
    }

    return true;
}

}
}
