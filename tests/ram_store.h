#ifndef TESTS_RAM_STORE_H
#define TESTS_RAM_STORE_H

/** @file ram_store.h
 *  @brief A VarStore that keeps its keys in RAM, so the save verbs have a
 *         storage side that ACCEPTS. Without one they can only be tested
 *         refusing.
 */

#include <cstring>
#include <string>
#include <vector>

#include "libbotanik/vars/persist.h"

namespace tests { namespace test_store {

/** @brief One key, one blob. The prefix is part of the key, so a row's setting,
 *  its override and its retained copy cannot collide - exactly as they do not
 *  in flash. */
struct RamStore final : Botanik::Vars::VarStore {
    struct Row { std::string key; std::string bytes; };
    std::vector<Row> rows;
    /** @brief Storage saying no: what a refused write is tested against. */
    bool     refuse = false;
    uint16_t writes = 0;

    Row* find(const char* key) {
        for (Row& r : rows) if (r.key == key) return &r;
        return nullptr;
    }

    bool load(const char* key, void* out, uint16_t width, uint16_t* got) override {
        Row* r = find(key);
        if (r == nullptr) return false;
        const uint16_t n = (uint16_t)((r->bytes.size() < width) ? r->bytes.size() : width);
        std::memcpy(out, r->bytes.data(), n);
        if (got != nullptr) *got = n;
        return true;
    }
    bool save(const char* key, const void* in, uint16_t width) override {
        if (refuse) return false;
        writes++;
        Row* r = find(key);
        if (r == nullptr) { rows.push_back(Row{key, ""}); r = &rows.back(); }
        r->bytes.assign(static_cast<const char*>(in), width);
        return true;
    }
    bool remove(const char* key) override {
        for (size_t i = 0; i < rows.size(); i++)
            if (rows[i].key == key) { rows.erase(rows.begin() + i); return true; }
        return true;
    }

    /** @brief Whether @p name is stored under @p prefix ("" the setting, "ovr."
     *  the override, "ret." the retained copy). */
    bool stored(const char* name, const char* prefix = "") {
        return find((std::string(prefix) + name).c_str()) != nullptr;
    }
};

}}  // namespace tests::test_store

#endif  // TESTS_RAM_STORE_H
