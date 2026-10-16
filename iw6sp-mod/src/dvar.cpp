#include "dvar.h"

#include <Windows.h>
#include <cstdint>
#include <cstdio>

// ===========================================================================
//  Engine types (exact layout, from AlterWare iw6-mod src/client/game).
// ===========================================================================
union DvarValue
{
    bool          enabled;
    int           integer;
    unsigned int  unsignedInt;
    float         value;
    float         vector[4];
    const char*   string;
    char          color[4];
};

// name @0x00, flags @0x08, type @0x0C, modified @0x0D, current @0x10, ...
struct dvar_t
{
    const char*   name;
    unsigned int  flags;
    unsigned char type;
    bool          modified;
    DvarValue     current;   // 0x10
    DvarValue     latched;   // 0x20
    DvarValue     reset;     // 0x30
};

// ===========================================================================
//  Single-player (iw6sp64_ship.exe) absolute addresses, preferred base
//  0x140000000. These are build-specific; rebased to the real module base.
// ===========================================================================
namespace
{
    constexpr uintptr_t kPreferredBase = 0x140000000ull;

    constexpr uintptr_t ADDR_Dvar_FindVar        = 0x140429E70;
    constexpr uintptr_t ADDR_Dvar_ValueToString  = 0x14042E710;
    constexpr uintptr_t ADDR_Cbuf_AddText        = 0x1403B3050; // (int localClient, const char* text)
    constexpr uintptr_t ADDR_Dvar_SetFromString  = 0x14042D000; // (name, value, DvarSetSource)
    constexpr uintptr_t ADDR_sortedDvars         = 0x1458CBA60; // dvar_t* []
    constexpr uintptr_t ADDR_dvarCount           = 0x1458CBA3C; // int

    using Dvar_FindVar_t       = dvar_t*     (*)(const char* name);
    using Dvar_ValueToString_t = const char* (*)(dvar_t* dvar, DvarValue value);
    using Cbuf_AddText_t       = void        (*)(int localClientNum, const char* text);
    using Dvar_SetFromStr_t    = void        (*)(const char* name, const char* value, unsigned int source);

    Dvar_FindVar_t       g_findVar       = nullptr;
    Dvar_ValueToString_t g_valueToString = nullptr;
    Cbuf_AddText_t       g_cbuf          = nullptr;
    Dvar_SetFromStr_t    g_setFromString = nullptr;
    dvar_t**             g_sortedDvars   = nullptr;
    int*                 g_dvarCount     = nullptr;

    bool g_ready  = false;
    int  g_verify = 0; // 0 = unknown, 1 = verified, -1 = failed

    uintptr_t module_base()
    {
        static uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        return base;
    }

    template <typename T>
    T rebased(uintptr_t addr)
    {
        if (!addr) return nullptr;
        return reinterpret_cast<T>(module_base() + (addr - kPreferredBase));
    }

    // POD-only probe guarded against access violations; no C++ unwinding here.
    bool seh_probe()
    {
        __try
        {
            dvar_t* d = g_findVar("version"); // a dvar that always exists
            if (d)
            {
                volatile const char* n = d->name;
                volatile unsigned char t = d->type;
                (void)n; (void)t;
            }
            if (g_dvarCount)
            {
                volatile int c = *g_dvarCount;
                (void)c;
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    // Lazy self-test. Caches only SUCCESS - on failure it retries (throttled),
    // because at inject time the game's dvar system may not exist yet, and we
    // must not permanently disable ourselves before the game finishes loading.
    bool verified_now()
    {
        if (g_verify == 1)
            return true;
        if (!g_ready || !g_findVar)
            return false;

        static unsigned long long lastTry = 0;
        const unsigned long long now = GetTickCount64();
        if (lastTry != 0 && now - lastTry < 1000)
            return false; // throttle: probe at most once per second until it works
        lastTry = now;

        if (seh_probe())
        {
            g_verify = 1;
            return true;
        }
        return false;
    }
}

bool Dvar::Init()
{
    g_findVar       = rebased<Dvar_FindVar_t>(ADDR_Dvar_FindVar);
    g_valueToString = rebased<Dvar_ValueToString_t>(ADDR_Dvar_ValueToString);
    g_cbuf          = rebased<Cbuf_AddText_t>(ADDR_Cbuf_AddText);
    g_setFromString = rebased<Dvar_SetFromStr_t>(ADDR_Dvar_SetFromString);
    g_sortedDvars   = rebased<dvar_t**>(ADDR_sortedDvars);
    g_dvarCount     = rebased<int*>(ADDR_dvarCount);

    g_ready = (g_findVar != nullptr && g_cbuf != nullptr);
    return g_ready;
}

bool Dvar::Ready()    { return g_ready; }
bool Dvar::Verified() { return verified_now(); }

dvar_t* Dvar::FindVar(const char* name)
{
    if (!g_findVar || !verified_now())
        return nullptr;
    return g_findVar(name);
}

void Dvar::Execute(const char* line)
{
    if (!g_cbuf || !line || !verified_now())
        return;
    char buffer[1024];
    _snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "%s\n", line);
    g_cbuf(0, buffer);
}

void Dvar::SetString(const char* name, const char* value)
{
    if (!g_setFromString || !verified_now())
        return;
    g_setFromString(name, value, 1 /* DVAR_SOURCE_EXTERNAL */);
}

bool Dvar::GetBool(const char* name, bool fallback)
{
    dvar_t* d = FindVar(name);
    return d ? d->current.enabled : fallback;
}

int Dvar::GetInt(const char* name, int fallback)
{
    dvar_t* d = FindVar(name);
    return d ? d->current.integer : fallback;
}

float Dvar::GetFloat(const char* name, float fallback)
{
    dvar_t* d = FindVar(name);
    return d ? d->current.value : fallback;
}

bool Dvar::TableReady()
{
    return g_sortedDvars != nullptr && g_dvarCount != nullptr && verified_now();
}

int Dvar::Count()
{
    return (g_dvarCount && verified_now()) ? *g_dvarCount : 0;
}

const char* Dvar::NameAt(int index)
{
    if (!g_sortedDvars || !verified_now() || index < 0 || index >= Count())
        return nullptr;
    dvar_t* d = g_sortedDvars[index];
    return (d && d->name) ? d->name : nullptr;
}

const char* Dvar::ValueToString(const char* name)
{
    if (!g_valueToString || !verified_now())
        return nullptr;
    dvar_t* d = g_findVar(name);
    return d ? g_valueToString(d, d->current) : nullptr;
}

int Dvar::Diagnostics(SigStatus* out, int max)
{
    const bool v = verified_now();
    const SigStatus items[] = {
        { "Dvar_FindVar",      g_findVar       != nullptr && v, reinterpret_cast<uintptr_t>(g_findVar)       },
        { "Dvar_ValueToString",g_valueToString != nullptr && v, reinterpret_cast<uintptr_t>(g_valueToString) },
        { "Cbuf_AddText",      g_cbuf          != nullptr && v, reinterpret_cast<uintptr_t>(g_cbuf)          },
        { "Dvar_SetFromString",g_setFromString != nullptr && v, reinterpret_cast<uintptr_t>(g_setFromString) },
        { "sortedDvars",       g_sortedDvars   != nullptr && v, reinterpret_cast<uintptr_t>(g_sortedDvars)   },
        { "dvarCount",         g_dvarCount     != nullptr && v, reinterpret_cast<uintptr_t>(g_dvarCount)     },
    };
    int written = 0;
    for (const auto& item : items)
    {
        if (written >= max) break;
        out[written++] = item;
    }
    return written;
}
