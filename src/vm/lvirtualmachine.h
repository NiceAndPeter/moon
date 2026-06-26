/*
** Lua Virtual Machine - Encapsulation of VM operations
** See Copyright Notice in lua.h
*/

#ifndef lvirtualmachine_h
#define lvirtualmachine_h

#include "lobject.h"
#include "lvm.h"
#include "lgc.h"

// Forward declarations
struct lua_State;
struct CallInfo;
class Table;

/*
** VirtualMachine class - Encapsulates all VM operations
** Previously scattered as luaV_* free functions
*/
class VirtualMachine {
private:
    lua_State* L;

public:
    // Default constructor - L will be set via setLuaState()
    VirtualMachine() noexcept : L(nullptr) {}

    // Constructor - ties VM to specific lua_State
    explicit VirtualMachine(lua_State* state) noexcept : L(state) {}

    // Initialize or change the associated lua_State
    void setLuaState(lua_State* state) noexcept { L = state; }

    // Prevent copying (VM is tied to specific lua_State)
    VirtualMachine(const VirtualMachine&) = delete;
    VirtualMachine& operator=(const VirtualMachine&) = delete;

    // Allow moving
    VirtualMachine(VirtualMachine&&) noexcept = default;
    VirtualMachine& operator=(VirtualMachine&&) noexcept = default;

    // === EXECUTION === (lvm.cpp)
    void execute(CallInfo *ci);
    void finishOp();

    // === TYPE CONVERSIONS === (lvm_conversion.cpp)
    [[nodiscard]] int tonumber(const TValue *obj, lua_Number *n) const;
    [[nodiscard]] int tointeger(const TValue *obj, lua_Integer *p, F2Imod mode) const;
    [[nodiscard]] int tointegerns(const TValue *obj, lua_Integer *p, F2Imod mode) const;
    [[nodiscard]] static int flttointeger(lua_Number n, lua_Integer *p, F2Imod mode);

    // === ARITHMETIC === (lvm_arithmetic.cpp)
    [[nodiscard]] lua_Integer idiv(lua_Integer m, lua_Integer n) const;
    [[nodiscard]] lua_Integer mod(lua_Integer m, lua_Integer n) const;
    [[nodiscard]] lua_Number modf(lua_Number m, lua_Number n) const;
    [[nodiscard]] static lua_Integer shiftl(lua_Integer x, lua_Integer y);

    // Shift right (inline - delegates to shiftl with negated shift amount)
    [[nodiscard]] static inline lua_Integer shiftr(lua_Integer x, lua_Integer y) noexcept {
        return shiftl(x, intop(-, 0, y));
    }

    // === COMPARISONS === (lvm_comparison.cpp)
    [[nodiscard]] int lessThan(const TValue *l, const TValue *r) const;
    [[nodiscard]] int lessEqual(const TValue *l, const TValue *r) const;
    [[nodiscard]] int equalObj(const TValue *t1, const TValue *t2) const;

    // Raw equality (inline - no metamethods, uses operator==)
    [[nodiscard]] static inline int rawequalObj(const TValue* t1, const TValue* t2) noexcept {
        return *t1 == *t2;
    }

    // === TABLE OPERATIONS === (lvm_table.cpp)
    [[nodiscard]] LuaT finishGet(const TValue *t, TValue *key, StkId val, LuaT tag) const;
    void finishSet(const TValue *t, TValue *key, TValue *val, int aux) const;

    // Fast-path table get - inline template for performance
    template<typename F>
    [[nodiscard]] inline LuaT fastget(const TValue* t, const TValue* k, TValue* res, F&& f) const noexcept {
        if (!ttistable(t))
            return LuaT::NOTABLE;
        return f(hvalue(t), k, res);
    }

    // Overload for TString* keys
    template<typename F>
    [[nodiscard]] inline LuaT fastget(const TValue* t, TString* k, TValue* res, F&& f) const noexcept {
        if (!ttistable(t))
            return LuaT::NOTABLE;
        return f(hvalue(t), k, res);
    }

    // Fast-path table get for integer keys - inline
    inline void fastgeti(const TValue* t, lua_Integer k, TValue* res, LuaT& tag) const noexcept {
        if (!ttistable(t))
            tag = LuaT::NOTABLE;
        else
            hvalue(t)->fastGeti(k, res, tag);
    }

    // Fast-path table set - inline template
    template<typename F>
    [[nodiscard]] inline int fastset(const TValue* t, const TValue* k, TValue* val, F&& f) const noexcept {
        if (!ttistable(t))
            return HNOTATABLE;
        return f(hvalue(t), k, val);
    }

    // Overload for TString* keys
    template<typename F>
    [[nodiscard]] inline int fastset(const TValue* t, TString* k, TValue* val, F&& f) const noexcept {
        if (!ttistable(t))
            return HNOTATABLE;
        return f(hvalue(t), k, val);
    }

    // Fast-path table set for integer keys - inline
    inline void fastseti(const TValue* t, lua_Integer k, TValue* val, int& hres) const noexcept {
        if (!ttistable(t))
            hres = HNOTATABLE;
        else
            hvalue(t)->fastSeti(k, val, hres);
    }

    // Finish fast set operation with GC barrier - inline
    inline void finishfastset(const TValue* t, const TValue* v) const noexcept {
        luaC_barrierback(L, gcvalue(t), v);
    }

    // === STRING/OBJECT OPERATIONS === (lvm_string.cpp)
    void concat(int total);
    void objlen(StkId ra, const TValue *rb);
};

#endif
