//
// WebAssembly representation and processing library
//

#ifndef __wasm_h__
#define __wasm_h__

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

#include "simple_ast.h"
#include "colors.h"

namespace wasm {

// Utilities

// Arena allocation for mixed-type data.
struct Arena {
  std::vector<char*> chunks;
  int index; // in last chunk

  template<class T>
  T* alloc() {
    const size_t CHUNK = 10000;
    size_t currSize = (sizeof(T) + 7) & (-8); // same alignment as malloc TODO optimize?
    assert(currSize < CHUNK);
    if (chunks.size() == 0 || index + currSize >= CHUNK) {
      chunks.push_back(new char[CHUNK]);
      index = 0;
    }
    T* ret = (T*)(chunks.back() + index);
    index += currSize;
    new (ret) T();
    return ret;
  }

  void clear() {
    for (char* chunk : chunks) {
      delete[] chunk;
    }
    chunks.clear();
  }

  ~Arena() {
    clear();
  }
};

std::ostream &doIndent(std::ostream &o, unsigned indent) {
  for (unsigned i = 0; i < indent; i++) {
    o << "  ";
  }
  return o;
}
std::ostream &incIndent(std::ostream &o, unsigned& indent) {
  o << '\n';
  indent++;
  return o; 
}
std::ostream &decIndent(std::ostream &o, unsigned& indent) {
  indent--;
  doIndent(o, indent);
  return o << ')';
}

// Basics

struct Name : public cashew::IString {
  Name() : cashew::IString() {}
  Name(const char *str) : cashew::IString(str) {}
  Name(cashew::IString str) : cashew::IString(str) {}

  friend std::ostream& operator<<(std::ostream &o, Name name) {
    assert(name.str);
    return o << '$' << name.str; // reference interpreter requires we prefix all names
  }
};

// Types

enum WasmType {
  none,
  i32,
  i64,
  f32,
  f64
};

const char* printWasmType(WasmType type) {
  switch (type) {
    case WasmType::none: return "none";
    case WasmType::i32: return "i32";
    case WasmType::i64: return "i64";
    case WasmType::f32: return "f32";
    case WasmType::f64: return "f64";
  }
}

unsigned getWasmTypeSize(WasmType type) {
  switch (type) {
    case WasmType::none: abort();
    case WasmType::i32: return 4;
    case WasmType::i64: return 8;
    case WasmType::f32: return 4;
    case WasmType::f64: return 8;
  }
}

bool isFloat(WasmType type) {
  switch (type) {
    case f32:
    case f64: return true;
    default: return false;
  }
}

WasmType getWasmType(unsigned size, bool float_) {
  if (size < 4) return WasmType::i32;
  if (size == 4) return float_ ? WasmType::f32 : WasmType::i32;
  if (size == 8) return float_ ? WasmType::f64 : WasmType::i64;
  abort();
}

std::ostream &prepareMajorColor(std::ostream &o) {
  Colors::red(o);
  Colors::bold(o);
  return o;
}

std::ostream &prepareColor(std::ostream &o) {
  Colors::magenta(o);
  Colors::bold(o);
  return o;
}

std::ostream &prepareMinorColor(std::ostream &o) {
  Colors::orange(o);
  return o;
}

std::ostream &restoreNormalColor(std::ostream &o) {
  Colors::normal(o);
  return o;
}

std::ostream& printText(std::ostream &o, const char *str) {
  o << '"';
  Colors::green(o);
  o << str;
  Colors::normal(o);
  return o << '"';
}

struct Literal {
  WasmType type;
  union {
    int32_t i32;
    int64_t i64;
    float f32;
    double f64;
  };

  Literal() : type(WasmType::none), f64(0) {}
  Literal(int32_t init) : type(WasmType::i32), i32(init) {}
  Literal(int64_t init) : type(WasmType::i64), i64(init) {}
  Literal(float   init) : type(WasmType::f32), f32(init) {}
  Literal(double  init) : type(WasmType::f64), f64(init) {}

  int32_t geti32() { assert(type == WasmType::i32); return i32; }
  int64_t geti64() { assert(type == WasmType::i64); return i64; }
  float   getf32() { assert(type == WasmType::f32); return f32; }
  double  getf64() { assert(type == WasmType::f64); return f64; }

  void printDouble(std::ostream &o, double d) {
    const char *text = cashew::JSPrinter::numToString(d);
    // spec interpreter hates floats starting with '.'
    if (text[0] == '.') {
      o << '0';
    } else if (text[0] == '-' && text[1] == '.') {
      o << "-0";
      text++;
    }
    o << text;
  }

  friend std::ostream& operator<<(std::ostream &o, Literal literal) {
    o << '(';
    prepareMinorColor(o) << printWasmType(literal.type) << ".const ";
    switch (literal.type) {
      case none: abort();
      case WasmType::i32: o << literal.i32; break;
      case WasmType::i64: o << literal.i64; break;
      case WasmType::f32: literal.printDouble(o, literal.f32); break;
      case WasmType::f64: literal.printDouble(o, literal.f64); break;
    }
    restoreNormalColor(o);
    return o << ')';
  }
};

// Operators

enum UnaryOp {
  Clz, Ctz, Popcnt, // int
  Neg, Abs, Ceil, Floor, Trunc, Nearest, Sqrt // float
};

enum BinaryOp {
  Add, Sub, Mul, // int or float
  DivS, DivU, RemS, RemU, And, Or, Xor, Shl, ShrU, ShrS, // int
  Div, CopySign, Min, Max // float
};

enum RelationalOp {
  Eq, Ne, // int or float
  LtS, LtU, LeS, LeU, GtS, GtU, GeS, GeU, // int
  Lt, Le, Gt, Ge // float
};

enum ConvertOp {
  ExtendSInt32, ExtendUInt32, WrapInt64, TruncSFloat32, TruncUFloat32, TruncSFloat64, TruncUFloat64, ReinterpretFloat, // int
  ConvertSInt32, ConvertUInt32, ConvertSInt64, ConvertUInt64, PromoteFloat32, DemoteFloat64, ReinterpretInt // float
};

enum HostOp {
  PageSize, MemorySize, GrowMemory, HasFeature
};

// Expressions

class Expression {
public:
  enum Id {
    InvalidId = 0,
    BlockId = 1,
    IfId = 2,
    LoopId = 3,
    LabelId = 4,
    BreakId = 5,
    SwitchId =6 ,
    CallId = 7,
    CallImportId = 8,
    CallIndirectId = 9,
    GetLocalId = 10,
    SetLocalId = 11,
    LoadId = 12,
    StoreId = 13,
    ConstId = 14,
    UnaryId = 15,
    BinaryId = 16,
    CompareId = 17,
    ConvertId = 18,
    HostId = 19,
    NopId = 20
  };
  Id _id;

  Expression() : _id(InvalidId) {}
  Expression(Id id) : _id(id) {}

  WasmType type; // the type of the expression: its output, not necessarily its input(s)

  std::ostream& print(std::ostream &o, unsigned indent); // avoid virtual here, for performance

  template<class T>
  bool is() {
    return _id == T()._id;
  }

  template<class T>
  T* dyn_cast() {
    return _id == T()._id ? (T*)this : nullptr;
  }
};

std::ostream& printFullLine(std::ostream &o, unsigned indent, Expression *expression) {
  doIndent(o, indent);
  expression->print(o, indent);
  return o << '\n';
}

std::ostream& printOpening(std::ostream &o, const char *str, bool major=false) {
  o << '(';
  major ? prepareMajorColor(o) : prepareColor(o);
  o << str;
  restoreNormalColor(o);
  return o;
}

std::ostream& printMinorOpening(std::ostream &o, const char *str) {
  o << '(';
  prepareMinorColor(o);
  o << str;
  restoreNormalColor(o);
  return o;
}

typedef std::vector<Expression*> ExpressionList; // TODO: optimize  

class Nop : public Expression {
public:
  Nop() : Expression(NopId) {}

  std::ostream& doPrint(std::ostream &o, unsigned indent) {
    return printMinorOpening(o, "nop") << ')';
  }
};

class Block : public Expression {
public:
  Block() : Expression(BlockId) {}

  Name name;
  ExpressionList list;

  std::ostream& doPrint(std::ostream &o, unsigned indent) {
    printOpening(o, "block");
    if (name.is()) {
      o << ' ' << name;
    }
    incIndent(o, indent);
    for (auto expression : list) {
      printFullLine(o, indent, expression);
    }
    return decIndent(o, indent);
  }
};

class If : public Expression {
public:
  If() : Expression(IfId), ifFalse(nullptr) {}

  Expression *condition, *ifTrue, *ifFalse;

  std::ostream& doPrint(std::ostream &o, unsigned indent) {
    printOpening(o, "if");
    incIndent(o, indent);
    printFullLine(o, indent, condition);
    printFullLine(o, indent, ifTrue);
    if (ifFalse) printFullLine(o, indent, ifFalse);
    return decIndent(o, indent);
  }
};

class Loop : public Expression {
public:
  Loop() : Expression(LoopId) {}

  Name out, in;
  Expression *body;

  std::ostream& doPrint(std::ostream &o, unsigned indent) {
    printOpening(o, "loop");
    if (out.is()) {
      o << ' ' << out;
      if (in.is()) {
        o << ' ' << in;
        }
    }
    incIndent(o, indent);
    printFullLine(o, indent, body);
    return decIndent(o, indent);
  }
};

class Label : public Expression {
public:
  Label() : Expression(LabelId) {}

  Name name;
  Expression* body;

  std::ostream& doPrint(std::ostream &o, unsigned indent) {
    printOpening(o, "label ") << name;
    incIndent(o, indent);
    printFullLine(o, indent, body);
    return decIndent(o, indent);
  }
};

class Break : public Expression {
public:
  Break() : Expression(BreakId), value(nullptr) {}

  Name name;
  Expression *value;

  std::ostream& doPrint(std::ostream &o, unsigned indent) {
    printOpening(o, "break ") << name;
    incIndent(o, indent);
    if (value) printFullLine(o, indent, value);
    return decIndent(o, indent);
  }
};

class Switch : public Expression {
public:
  Switch() : Expression(SwitchId) {}

  struct Case {
    Name name;
    Expression *body;
  };

  Name name;
  Expression *value;
  std::vector<Name> targets;
  Name default_;
  std::vector<Case> cases;
  std::map<Name, size_t> caseMap; // name => index in cases

  void updateCaseMap() {
    for (size_t i = 0; i < cases.size(); i++) {
      caseMap[cases[i].name] = i;
    }
  }

  std::ostream& doPrint(std::ostream &o, unsigned indent) {
    printOpening(o, "switch ") << name;
    incIndent(o, indent);
    printFullLine(o, indent, value);
    doIndent(o, indent) << "[ ";
    for (auto& t : targets) {
      o << t.str << ' ';
    }
    o << "] (default " << default_.str << ")\n";
    for (auto& c : cases) {
      doIndent(o, indent);
      printMinorOpening(o, "case ") << c.name.str;
      incIndent(o, indent);
      printFullLine(o, indent, c.body);
      decIndent(o, indent) << '\n';
    }
    return decIndent(o, indent);
  }

};

class Call : public Expression {
public:
  Call() : Expression(CallId) {}

  Name target;
  ExpressionList operands;

  std::ostream& printBody(std::ostream &o, unsigned indent) {
    o << target;
    if (operands.size() > 0) {
      incIndent(o, indent);
      for (auto operand : operands) {
        printFullLine(o, indent, operand);
      }
      decIndent(o, indent);
    } else {
      o << ')';
    }
    return o;
  }

  std::ostream& doPrint(std::ostream &o, unsigned indent) {
    printOpening(o, "call ");
    return printBody(o, indent);
  }
};

class CallImport : public Call {
public:
  CallImport() {
    _id = CallImportId;
  }

  std::ostream& doPrint(std::ostream &o, unsigned indent) {
    printOpening(o, "call_import ");
    return printBody(o, indent);
  }
};

class FunctionType {
public:
  Name name;
  WasmType result;
  std::vector<WasmType> params;

  std::ostream& print(std::ostream &o, unsigned indent, bool full=false) {
    if (full) {
      printOpening(o, "type") << ' ' << name << " (func";
    }
    if (params.size() > 0) {
      o << ' ';
      printMinorOpening(o, "param");
      for (auto& param : params) {
        o << ' ' << printWasmType(param);
      }
      o << ')';
    }
    if (result != none) {
      o << ' ';
      printMinorOpening(o, "result ") << printWasmType(result) << ')';
    }
    if (full) {
      o << "))";;
    }
    return o;
  }

  bool operator==(FunctionType& b) {
    if (name != b.name) return false; // XXX
    if (result != b.result) return false;
    if (params.size() != b.params.size()) return false;
    for (size_t i = 0; i < params.size(); i++) {
      if (params[i] != b.params[i]) return false;
    }
    return true;
  }
  bool operator!=(FunctionType& b) {
    return !(*this == b);
  }
};

class CallIndirect : public Expression {
public:
  CallIndirect() : Expression(CallIndirectId) {}

  FunctionType *type;
  Expression *target;
  ExpressionList operands;

  std::ostream& doPrint(std::ostream &o, unsigned indent) {
    printOpening(o, "call_indirect ") << type->name;
    incIndent(o, indent);
    printFullLine(o, indent, target);
    for (auto operand : operands) {
      printFullLine(o, indent, operand);
    }
    return decIndent(o, indent);
  }
};

class GetLocal : public Expression {
public:
  GetLocal() : Expression(GetLocalId) {}

  Name name;

  std::ostream& doPrint(std::ostream &o, unsigned indent) {
    return printOpening(o, "get_local ") << name << ')';
  }
};

class SetLocal : public Expression {
public:
  SetLocal() : Expression(SetLocalId) {}

  Name name;
  Expression *value;

  std::ostream& doPrint(std::ostream &o, unsigned indent) {
    printOpening(o, "set_local ") << name;
    incIndent(o, indent);
    printFullLine(o, indent, value);
    return decIndent(o, indent);
  }
};

class Load : public Expression {
public:
  Load() : Expression(LoadId) {}

  unsigned bytes;
  bool signed_;
  bool float_;
  int offset;
  unsigned align;
  Expression *ptr;

  std::ostream& doPrint(std::ostream &o, unsigned indent) {
    o << '(';
    prepareColor(o) << printWasmType(getWasmType(bytes, float_)) << ".load";
    if (bytes < 4) {
      if (bytes == 1) {
        o << '8';
      } else if (bytes == 2) {
        o << "16";
      } else {
        abort();
      }
      o << (signed_ ? "_s" : "_u");
    }
    restoreNormalColor(o);
    o << " align=" << align;
    assert(!offset);
    incIndent(o, indent);
    printFullLine(o, indent, ptr);
    return decIndent(o, indent);
  }
};

class Store : public Expression {
public:
  Store() : Expression(StoreId) {}

  unsigned bytes;
  bool float_;
  int offset;
  unsigned align;
  Expression *ptr, *value;

  std::ostream& doPrint(std::ostream &o, unsigned indent) {
    o << '(';
    prepareColor(o) << printWasmType(getWasmType(bytes, float_)) << ".store";
    if (bytes < 4) {
      if (bytes == 1) {
        o << '8';
      } else if (bytes == 2) {
        o << "16";
      } else {
        abort();
      }
    }
    restoreNormalColor(o);
    o << " align=" << align;
    assert(!offset);
    incIndent(o, indent);
    printFullLine(o, indent, ptr);
    printFullLine(o, indent, value);
    return decIndent(o, indent);
  }
};

class Const : public Expression {
public:
  Const() : Expression(ConstId) {}

  Literal value;

  Const* set(Literal value_) {
    value = value_;
    type = value.type;
    return this;
  }

  std::ostream& doPrint(std::ostream &o, unsigned indent) {
    return o << value;
  }
};

class Unary : public Expression {
public:
  Unary() : Expression(UnaryId) {}

  UnaryOp op;
  Expression *value;

  std::ostream& doPrint(std::ostream &o, unsigned indent) {
    o << '(';
    prepareColor(o) << printWasmType(type) << '.';
    switch (op) {
      case Clz: o << "clz"; break;
      case Neg: o << "neg"; break;
      case Floor: o << "floor"; break;
      default: abort();
    }
    incIndent(o, indent);
    printFullLine(o, indent, value);
    return decIndent(o, indent);
  }
};

class Binary : public Expression {
public:
  Binary() : Expression(BinaryId) {}

  BinaryOp op;
  Expression *left, *right;

  std::ostream& doPrint(std::ostream &o, unsigned indent) {
    o << '(';
    prepareColor(o) << printWasmType(type) << '.';
    switch (op) {
      case Add:      o << "add"; break;
      case Sub:      o << "sub"; break;
      case Mul:      o << "mul"; break;
      case DivS:     o << "div_s"; break;
      case DivU:     o << "div_u"; break;
      case RemS:     o << "rem_s"; break;
      case RemU:     o << "rem_u"; break;
      case And:      o << "and"; break;
      case Or:       o << "or"; break;
      case Xor:      o << "xor"; break;
      case Shl:      o << "shl"; break;
      case ShrU:     o << "shr_u"; break;
      case ShrS:     o << "shr_s"; break;
      case Div:      o << "div"; break;
      case CopySign: o << "copysign"; break;
      case Min:      o << "min"; break;
      case Max:      o << "max"; break;
      default: abort();
    }
    restoreNormalColor(o);
    incIndent(o, indent);
    printFullLine(o, indent, left);
    printFullLine(o, indent, right);
    return decIndent(o, indent);
  }
};

class Compare : public Expression {
public:
  Compare() : Expression(CompareId) {
    type = WasmType::i32; // output is always i32
  }

  RelationalOp op;
  WasmType inputType;
  Expression *left, *right;

  std::ostream& doPrint(std::ostream &o, unsigned indent) {
    o << '(';
    prepareColor(o) << printWasmType(inputType) << '.';
    switch (op) {
      case Eq:  o << "eq"; break;
      case Ne:  o << "ne"; break;
      case LtS: o << "lt_s"; break;
      case LtU: o << "lt_u"; break;
      case LeS: o << "le_s"; break;
      case LeU: o << "le_u"; break;
      case GtS: o << "gt_s"; break;
      case GtU: o << "gt_u"; break;
      case GeS: o << "ge_s"; break;
      case GeU: o << "ge_u"; break;
      case Lt:  o << "lt"; break;
      case Le:  o << "le"; break;
      case Gt:  o << "gt"; break;
      case Ge:  o << "ge"; break;
      default: abort();
    }
    restoreNormalColor(o);
    incIndent(o, indent);
    printFullLine(o, indent, left);
    printFullLine(o, indent, right);
    return decIndent(o, indent);
  }
};

class Convert : public Expression {
public:
  Convert() : Expression(ConvertId) {}

  ConvertOp op;
  Expression *value;

  std::ostream& doPrint(std::ostream &o, unsigned indent) {
    o << '(';
    prepareColor(o);
    switch (op) {
      case ConvertUInt32: o << "f64.convert_u/i32"; break;
      case ConvertSInt32: o << "f64.convert_s/i32"; break;
      case TruncSFloat64: o << "i32.trunc_s/f64"; break;
      default: abort();
    }
    restoreNormalColor(o);
    incIndent(o, indent);
    printFullLine(o, indent, value);
    return decIndent(o, indent);
  }
};

class Host : public Expression {
public:
  Host() : Expression(HostId) {}

  HostOp op;
  ExpressionList operands;

  std::ostream& doPrint(std::ostream &o, unsigned indent) {
    return printOpening(o, "host") << ')';
  }
};

// Globals

struct NameType {
  Name name;
  WasmType type;
  NameType() : name(nullptr), type(none) {}
  NameType(Name name, WasmType type) : name(name), type(type) {}
};

class Function {
public:
  Name name;
  WasmType result;
  std::vector<NameType> params;
  std::vector<NameType> locals;
  Expression *body;

  std::ostream& print(std::ostream &o, unsigned indent) {
    printOpening(o, "func ", true) << name;
    if (params.size() > 0) {
      for (auto& param : params) {
        o << ' ';
        printMinorOpening(o, "param ") << param.name << ' ' << printWasmType(param.type) << ")";
      }
    }
    if (result != none) {
      o << ' ';
      printMinorOpening(o, "result ") << printWasmType(result) << ")";
    }
    incIndent(o, indent);
    for (auto& local : locals) {
      doIndent(o, indent);
      printMinorOpening(o, "local ") << local.name << ' ' << printWasmType(local.type) << ")\n";
    }
    printFullLine(o, indent, body);
    return decIndent(o, indent);
  }
};

class Import {
public:
  Name name, module, base; // name = module.base
  FunctionType type;

  std::ostream& print(std::ostream &o, unsigned indent) {
    printOpening(o, "import ") << name << ' ';
    printText(o, module.str) << ' ';
    printText(o, base.str) << ' ';
    type.print(o, indent);
    return o << ')';
  }
};

class Export {
public:
  Name name;
  Name value;

  std::ostream& print(std::ostream &o, unsigned indent) {
    printOpening(o, "export ");
    return printText(o, name.str) << ' ' << value << ')';
  }
};

class Table {
public:
  std::vector<Name> names;

  std::ostream& print(std::ostream &o, unsigned indent) {
    printOpening(o, "table");
    for (auto name : names) {
      o << ' ' << name;
    }
    return o << ')';
  }
};

class Module {
public:
  // wasm contents
  std::map<Name, FunctionType*> functionTypes;
  std::map<Name, Import> imports;
  std::vector<Export> exports;
  Table table;
  std::vector<Function*> functions;

  Module() {}

  friend std::ostream& operator<<(std::ostream &o, Module module) {
    unsigned indent = 0;
    printOpening(o, "module", true);
    incIndent(o, indent);
    doIndent(o, indent);
    printOpening(o, "memory") << " 16777216)\n"; // XXX
    for (auto& curr : module.functionTypes) {
      doIndent(o, indent);
      curr.second->print(o, indent, true);
      o << '\n';
    }
#if 0
    for (auto& curr : module.imports) {
      doIndent(o, indent);
      curr.second.print(o, indent);
      o << '\n';
    }
#endif
    for (auto& curr : module.exports) {
      doIndent(o, indent);
      curr.print(o, indent);
      o << '\n';
    }
    if (module.table.names.size() > 0) {
      doIndent(o, indent);
      module.table.print(o, indent);
      o << '\n';
    }
    for (auto& curr : module.functions) {
      doIndent(o, indent);
      curr->print(o, indent);
      o << '\n';
    }
    decIndent(o, indent);
    return o << '\n';
  }
};

//
// Simple WebAssembly AST visiting
//

template<class ReturnType>
struct WasmVisitor {
  virtual ReturnType visitBlock(Block *curr) = 0;
  virtual ReturnType visitIf(If *curr) = 0;
  virtual ReturnType visitLoop(Loop *curr) = 0;
  virtual ReturnType visitLabel(Label *curr) = 0;
  virtual ReturnType visitBreak(Break *curr) = 0;
  virtual ReturnType visitSwitch(Switch *curr) = 0;
  virtual ReturnType visitCall(Call *curr) = 0;
  virtual ReturnType visitCallImport(CallImport *curr) = 0;
  virtual ReturnType visitCallIndirect(CallIndirect *curr) = 0;
  virtual ReturnType visitGetLocal(GetLocal *curr) = 0;
  virtual ReturnType visitSetLocal(SetLocal *curr) = 0;
  virtual ReturnType visitLoad(Load *curr) = 0;
  virtual ReturnType visitStore(Store *curr) = 0;
  virtual ReturnType visitConst(Const *curr) = 0;
  virtual ReturnType visitUnary(Unary *curr) = 0;
  virtual ReturnType visitBinary(Binary *curr) = 0;
  virtual ReturnType visitCompare(Compare *curr) = 0;
  virtual ReturnType visitConvert(Convert *curr) = 0;
  virtual ReturnType visitHost(Host *curr) = 0;
  virtual ReturnType visitNop(Nop *curr) = 0;

  ReturnType visit(Expression *curr) {
    assert(curr);
    switch (curr->_id) {
      case Expression::Id::BlockId: return visitBlock((Block*)curr);
      case Expression::Id::IfId: return visitIf((If*)curr);
      case Expression::Id::LoopId: return visitLoop((Loop*)curr);
      case Expression::Id::LabelId: return visitLabel((Label*)curr);
      case Expression::Id::BreakId: return visitBreak((Break*)curr);
      case Expression::Id::SwitchId: return visitSwitch((Switch*)curr);
      case Expression::Id::CallId: return visitCall((Call*)curr);
      case Expression::Id::CallImportId: return visitCallImport((CallImport*)curr);
      case Expression::Id::CallIndirectId: return visitCallIndirect((CallIndirect*)curr);
      case Expression::Id::GetLocalId: return visitGetLocal((GetLocal*)curr);
      case Expression::Id::SetLocalId: return visitSetLocal((SetLocal*)curr);
      case Expression::Id::LoadId: return visitLoad((Load*)curr);
      case Expression::Id::StoreId: return visitStore((Store*)curr);
      case Expression::Id::ConstId: return visitConst((Const*)curr);
      case Expression::Id::UnaryId: return visitUnary((Unary*)curr);
      case Expression::Id::BinaryId: return visitBinary((Binary*)curr);
      case Expression::Id::CompareId: return visitCompare((Compare*)curr);
      case Expression::Id::ConvertId: return visitConvert((Convert*)curr);
      case Expression::Id::HostId: return visitHost((Host*)curr);
      case Expression::Id::NopId: return visitNop((Nop*)curr);
      default: {
        std::cerr << "visiting unknown expression " << curr->_id << '\n';
        abort();
      }
    }
  }
};

std::ostream& Expression::print(std::ostream &o, unsigned indent) {
  struct ExpressionPrinter : public WasmVisitor<void> {
    std::ostream &o;
    unsigned indent;

    ExpressionPrinter(std::ostream &o, unsigned indent) : o(o), indent(indent) {}

    void visitBlock(Block *curr) override { curr->doPrint(o, indent); }
    void visitIf(If *curr) override { curr->doPrint(o, indent); }
    void visitLoop(Loop *curr) override { curr->doPrint(o, indent); }
    void visitLabel(Label *curr) override { curr->doPrint(o, indent); }
    void visitBreak(Break *curr) override { curr->doPrint(o, indent); }
    void visitSwitch(Switch *curr) override { curr->doPrint(o, indent); }
    void visitCall(Call *curr) override { curr->doPrint(o, indent); }
    void visitCallImport(CallImport *curr) override { curr->doPrint(o, indent); }
    void visitCallIndirect(CallIndirect *curr) override { curr->doPrint(o, indent); }
    void visitGetLocal(GetLocal *curr) override { curr->doPrint(o, indent); }
    void visitSetLocal(SetLocal *curr) override { curr->doPrint(o, indent); }
    void visitLoad(Load *curr) override { curr->doPrint(o, indent); }
    void visitStore(Store *curr) override { curr->doPrint(o, indent); }
    void visitConst(Const *curr) override { curr->doPrint(o, indent); }
    void visitUnary(Unary *curr) override { curr->doPrint(o, indent); }
    void visitBinary(Binary *curr) override { curr->doPrint(o, indent); }
    void visitCompare(Compare *curr) override { curr->doPrint(o, indent); }
    void visitConvert(Convert *curr) override { curr->doPrint(o, indent); }
    void visitHost(Host *curr) override { curr->doPrint(o, indent); }
    void visitNop(Nop *curr) override { curr->doPrint(o, indent); }
  };

  ExpressionPrinter(o, indent).visit(this);

  return o;
}

//
// Simple WebAssembly children-first walking
//

struct WasmWalker : public WasmVisitor<void> {
  wasm::Arena* allocator; // use an existing allocator, or null if no allocations
  Expression* replace;

  WasmWalker() : allocator(nullptr), replace(nullptr) {}
  WasmWalker(wasm::Arena* allocator) : allocator(allocator), replace(nullptr) {}

  // the visit* methods can call this to replace the current node
  void replaceCurrent(Expression *expression) {
    replace = expression;
  }

  // By default, do nothing
  void visitBlock(Block *curr) override {};
  void visitIf(If *curr) override {};
  void visitLoop(Loop *curr) override {};
  void visitLabel(Label *curr) override {};
  void visitBreak(Break *curr) override {};
  void visitSwitch(Switch *curr) override {};
  void visitCall(Call *curr) override {};
  void visitCallImport(CallImport *curr) override {};
  void visitCallIndirect(CallIndirect *curr) override {};
  void visitGetLocal(GetLocal *curr) override {};
  void visitSetLocal(SetLocal *curr) override {};
  void visitLoad(Load *curr) override {};
  void visitStore(Store *curr) override {};
  void visitConst(Const *curr) override {};
  void visitUnary(Unary *curr) override {};
  void visitBinary(Binary *curr) override {};
  void visitCompare(Compare *curr) override {};
  void visitConvert(Convert *curr) override {};
  void visitHost(Host *curr) override {};
  void visitNop(Nop *curr) override {};

  // children-first
  void walk(Expression*& curr) {
    if (!curr) return;

    struct ChildWalker : public WasmVisitor {
      WasmWalker& parent;

      ChildWalker(WasmWalker& parent) : parent(parent) {}

      void visitBlock(Block *curr) override {
        ExpressionList& list = curr->list;
        for (size_t z = 0; z < list.size(); z++) {
          parent.walk(list[z]);
        }
      }
      void visitIf(If *curr) override {
        parent.walk(curr->condition);
        parent.walk(curr->ifTrue);
        parent.walk(curr->ifFalse);
      }
      void visitLoop(Loop *curr) override {
        parent.walk(curr->body);
      }
      void visitLabel(Label *curr) override {}
      void visitBreak(Break *curr) override {
        parent.walk(curr->value);
      }
      void visitSwitch(Switch *curr) override {
        parent.walk(curr->value);
        for (auto& case_ : curr->cases) {
          parent.walk(case_.body);
        }
      }
      void visitCall(Call *curr) override {
        ExpressionList& list = curr->operands;
        for (size_t z = 0; z < list.size(); z++) {
          parent.walk(list[z]);
        }
      }
      void visitCallImport(CallImport *curr) override {
        ExpressionList& list = curr->operands;
        for (size_t z = 0; z < list.size(); z++) {
          parent.walk(list[z]);
        }
      }
      void visitCallIndirect(CallIndirect *curr) override {
        parent.walk(curr->target);
        ExpressionList& list = curr->operands;
        for (size_t z = 0; z < list.size(); z++) {
          parent.walk(list[z]);
        }
      }
      void visitGetLocal(GetLocal *curr) override {}
      void visitSetLocal(SetLocal *curr) override {
        parent.walk(curr->value);
      }
      void visitLoad(Load *curr) override {
        parent.walk(curr->ptr);
      }
      void visitStore(Store *curr) override {
        parent.walk(curr->ptr);
        parent.walk(curr->value);
      }
      void visitConst(Const *curr) override {}
      void visitUnary(Unary *curr) override {
        parent.walk(curr->value);
      }
      void visitBinary(Binary *curr) override {
        parent.walk(curr->left);
        parent.walk(curr->right);
      }
      void visitCompare(Compare *curr) override {
        parent.walk(curr->left);
        parent.walk(curr->right);
      }
      void visitConvert(Convert *curr) override {
        parent.walk(curr->value);
      }
      void visitHost(Host *curr) override {
        ExpressionList& list = curr->operands;
        for (size_t z = 0; z < list.size(); z++) {
          parent.walk(list[z]);
        }
      }
      void visitNop(Nop *curr) override {}
    };

    ChildWalker(*this).visit(curr);

    visit(curr);

    if (replace) {
      curr = replace;
      replace = nullptr;
    }
  }

  void startWalk(Function *func) {
    walk(func->body);
  }
};

} // namespace wasm

#endif // __wasm_h__

