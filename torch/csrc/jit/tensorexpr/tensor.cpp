#include <torch/csrc/jit/tensorexpr/tensor.h>

#include <c10/util/Logging.h>
#include <c10/util/irange.h>
#include <torch/csrc/jit/tensorexpr/dim_arg.h>
#include <torch/csrc/jit/tensorexpr/reduction.h>

namespace torch {
namespace jit {
namespace tensorexpr {

Stmt* Tensor::constructStmt(
    const std::vector<Var*>& args,
    Expr* body,
    const std::vector<Expr*>& reduce_dims,
    const std::vector<Var*>& reduce_args) const {
  std::vector<Expr*> indices(args.begin(), args.end());

  Stmt* s = new Store(buf_, indices, body);

  size_t ndim = buf()->ndim();
  size_t reduce_ndim = reduce_dims.size();

  if (ndim == 0 && reduce_ndim == 0) {
    return s;
  }

  Expr* init_expr = buf()->initializer();

  if (reduce_ndim > 0) {
    for (const auto i : c10::irange(reduce_ndim)) {
      // Going in reverse order: from innermost loop to the outermost
      size_t dim_index = reduce_ndim - i - 1;
      s = new For(
          reduce_args[dim_index], new IntImm(0), reduce_dims[dim_index], s);
    }
    if (init_expr) {
      Store* init_stmt = new Store(buf(), indices, init_expr);
      s = new Block({init_stmt, s});
    }
  }

  for (const auto i : c10::irange(ndim)) {
    // Going in reverse order: from innermost loop to the outermost
    size_t dim_index = ndim - i - 1;
    s = new For(args[dim_index], new IntImm(0), buf()->dim(dim_index), s);
  }
  return s;
}

Tensor* Compute(
    const std::string& name,
    const std::vector<DimArg>& dim_args,
    const std::function<ExprHandle(const std::vector<VarHandle>&)>& body_func) {
  std::vector<Expr*> dims;
  std::vector<Var*> args;
  unpack_dim_args(dim_args, &dims, &args);
  Expr* body = body_func(VarVectorToVarHandleVector(args)).node();
  Buf* buf = new Buf(name, dims, body->dtype());
  return new Tensor(buf, args, body);
}

Tensor* Compute(
    const std::string& name,
    const std::vector<DimArg>& dim_args,
    const std::function<ExprHandle(const VarHandle&)>& body_func) {
  if (dim_args.size() != 1) {
    throw malformed_input("mismatch between body and arg size (1)");
  }

  std::vector<Expr*> dims;
  std::vector<Var*> args;
  unpack_dim_args(dim_args, &dims, &args);
  Expr* body = body_func(VarHandle(args[0])).node();
  Buf* buf = new Buf(name, dims, body->dtype());
  return new Tensor(buf, args, body);
}

Tensor* Compute(
    const std::string& name,
    const std::vector<DimArg>& dim_args,
    const std::function<ExprHandle(const VarHandle&, const VarHandle&)>&
        body_func) {
  if (dim_args.size() != 2) {
    throw malformed_input("mismatch between body and arg size (2)");
  }
  std::vector<Expr*> dims;
  std::vector<Var*> args;
  unpack_dim_args(dim_args, &dims, &args);
  Expr* body = body_func(VarHandle(args[0]), VarHandle(args[1])).node();
  Buf* buf = new Buf(name, dims, body->dtype());
  return new Tensor(buf, args, body);
}

Tensor* Compute(
    const std::string& name,
    const std::vector<DimArg>& dim_args,
    const std::function<
        ExprHandle(const VarHandle&, const VarHandle&, const VarHandle&)>&
        body_func) {
  if (dim_args.size() != 3) {
    throw malformed_input("mismatch between body and arg size (3)");
  }
  std::vector<Expr*> dims;
  std::vector<Var*> args;
  unpack_dim_args(dim_args, &dims, &args);
  Expr* body =
      body_func(VarHandle(args[0]), VarHandle(args[1]), VarHandle(args[2]))
          .node();
  Buf* buf = new Buf(name, dims, body->dtype());
  return new Tensor(buf, args, body);
}

Tensor* Compute(
    const std::string& name,
    const std::vector<DimArg>& dim_args,
    const std::function<ExprHandle(
        const VarHandle&,
        const VarHandle&,
        const VarHandle&,
        const VarHandle&)>& body_func) {
  if (dim_args.size() != 4) {
    throw malformed_input("mismatch between body and arg size (4)");
  }
  std::vector<Expr*> dims;
  std::vector<Var*> args;
  unpack_dim_args(dim_args, &dims, &args);
  Expr* body = body_func(
                   VarHandle(args[0]),
                   VarHandle(args[1]),
                   VarHandle(args[2]),
                   VarHandle(args[3]))
                   .node();
  Buf* buf = new Buf(name, dims, body->dtype());
  return new Tensor(buf, args, body);
}

Tensor* Reduce(
    const std::string& name,
    const std::vector<DimArg>& dim_args,
    const Reducer& reducer,
    const Placeholder& buffer,
    const std::vector<DimArg>& reduce_args) {
  return Reduce(
      name,
      dim_args,
      reducer,
      [&](ParameterList& p) { return buffer.load(p); },
      reduce_args);
}

Tensor* Reduce(
    const std::string& name,
    const std::vector<DimArg>& dim_args,
    const Reducer& reducer,
    const BufHandle& buffer,
    const std::vector<DimArg>& reduce_args) {
  return Reduce(
      name,
      dim_args,
      reducer,
      [&](ParameterList& p) { return buffer.load(p); },
      reduce_args);
}

Tensor* Reduce(
    const std::string& name,
    const std::vector<DimArg>& dim_args,
    const Reducer& reducer,
    Tensor* tensor,
    const std::vector<DimArg>& reduce_args) {
  return Reduce(
      name,
      dim_args,
      reducer,
      [&](ParameterList& p) { return tensor->load(p); },
      reduce_args);
}

} // namespace tensorexpr
} // namespace jit
} // namespace torch
