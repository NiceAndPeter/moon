/*
** Lua Virtual Machine - Encapsulation of VM operations
** See Copyright Notice in lua.h
*/

#ifndef lvirtualmachine_h
#define lvirtualmachine_h

#include "mobject.h"
#include "mvm.h"
#include "mgc.h"

// Forward declarations
struct moon_State;
struct CallInfo;
class Table;

/*
** VirtualMachine class - Encapsulates all VM operations
** Previously scattered as moonV_* free functions
*/
class VirtualMachine {
private:
    moon_State* L;

public:
    // Default constructor - L will be set via setLuaState()
    VirtualMachine() noexcept : L(nullptr) {}

    // Constructor - ties VM to specific moon_State
    explicit VirtualMachine(moon_State* state) noexcept : L(state) {}

    // Initialize or change the associated moon_State
    void setLuaState(moon_State* state) noexcept { L = state; }

    // Prevent copying (VM is tied to specific moon_State)
    VirtualMachine(const VirtualMachine&) = delete;
    VirtualMachine& operator=(const VirtualMachine&) = delete;

    // Allow moving
    VirtualMachine(VirtualMachine&&) noexcept = default;
    VirtualMachine& operator=(VirtualMachine&&) noexcept = default;

    // === EXECUTION === (lvm.cpp)
    void execute(CallInfo *callInfo);
    void finishOp();

    // === TYPE CONVERSIONS === (lvm_conversion.cpp)
    [[nodiscard]] int tonumber(const TValue *obj, moon_Number *n) const;
    [[nodiscard]] int tointeger(const TValue *obj, moon_Integer *p, F2Imod mode) const;
    [[nodiscard]] int tointegerns(const TValue *obj, moon_Integer *p, F2Imod mode) const;
    [[nodiscard]] static int flttointeger(moon_Number n, moon_Integer *p, F2Imod mode);

    // === ARITHMETIC === (lvm_arithmetic.cpp)
    [[nodiscard]] moon_Integer idiv(moon_Integer m, moon_Integer n) const;
    [[nodiscard]] moon_Integer mod(moon_Integer m, moon_Integer n) const;
    [[nodiscard]] moon_Number modf(moon_Number m, moon_Number n) const;
    [[nodiscard]] static moon_Integer shiftl(moon_Integer x, moon_Integer y);

    // Shift right (inline - delegates to shiftl with negated shift amount)
    [[nodiscard]] static inline moon_Integer shiftr(moon_Integer x, moon_Integer y) noexcept {
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
    [[nodiscard]] MoonT finishGet(const TValue *t, TValue *key, StkId val, MoonT tag) const;
    void finishSet(const TValue *t, TValue *key, TValue *val, int aux) const;

    // Fast-path table get - inline template for performance
    template<typename F>
    [[nodiscard]] inline MoonT fastget(const TValue* t, const TValue* k, TValue* res, F&& f) const noexcept {
        if (!ttistable(t))
            return MoonT::NOTABLE;
        return f(hvalue(t), k, res);
    }

    // Overload for TString* keys
    template<typename F>
    [[nodiscard]] inline MoonT fastget(const TValue* t, TString* k, TValue* res, F&& f) const noexcept {
        if (!ttistable(t))
            return MoonT::NOTABLE;
        return f(hvalue(t), k, res);
    }

    // Fast-path table get for integer keys - inline
    inline void fastgeti(const TValue* t, moon_Integer k, TValue* res, MoonT& tag) const noexcept {
        if (!ttistable(t))
            tag = MoonT::NOTABLE;
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
    inline void fastseti(const TValue* t, moon_Integer k, TValue* val, int& hres) const noexcept {
        if (!ttistable(t))
            hres = HNOTATABLE;
        else
            hvalue(t)->fastSeti(k, val, hres);
    }

    // Finish fast set operation with GC barrier - inline
    inline void finishfastset(const TValue* t, const TValue* v) const noexcept {
        moonC_barrierback(L, gcvalue(t), v);
    }

    // === STRING/OBJECT OPERATIONS === (lvm_string.cpp)
    void concat(int total);
    void objlen(StkId ra, const TValue *rb);
};

#endif
