#pragma once

#include <algorithm>
#include <list>
#include <string>
#include <unordered_set>
#include <vector>

#include <torch/csrc/jit/tensorexpr/expr.h>
namespace torch {
namespace jit {
namespace tensorexpr {

class Placeholder;

// The common base between all statement node.
class TORCH_API Stmt : public KernelScopedObject {
 public:
  Stmt() = default;
  virtual void accept(IRVisitor* visitor) = 0;
  virtual Stmt* accept_mutator(IRMutator* mutator) = 0;

  Stmt* get_parent() const {
    return parent_;
  }

  /*
   * Make a deep copy of the given statement.
   *
   * All statements and expressions used in children of the statement are
   * cloned. Note that the variables are not deep-copied since they are
   * immutable.
   */
  static Stmt* clone(Stmt* s);

 protected:
  static void set_parent(Stmt* s, Stmt* new_parent) {
    s->parent_ = new_parent;
  }

 private:
  Stmt* parent_ = nullptr;
};

template <class Op>
class StmtNode : public Stmt {
 public:
  using StmtNodeBase = StmtNode<Op>;
  void accept(IRVisitor* visitor) override {
    visitor->visit(static_cast<Op*>(this));
  }
  Stmt* accept_mutator(IRMutator* mutator) override;
  StmtNode() = default;
};

template <class Op>
Stmt* StmtNode<Op>::accept_mutator(IRMutator* mutator) {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
  StmtNode* this_mutable = const_cast<StmtNode*>(this);
  return mutator->mutate(static_cast<Op*>(this_mutable));
}

// Concrete Stmt classes
class TORCH_API Block : public StmtNode<Block> {
 public:
  static Block* make(const std::vector<Stmt*>& stmts) {
    // NOLINTNEXTLINE(cppcoreguidelines-init-variables)
    std::vector<Stmt*> valid_stmts;
    for (auto& stmt : stmts) {
      if (!stmt) {
        continue;
      }
      valid_stmts.push_back(stmt);
    }
    if (valid_stmts.empty()) {
      return nullptr;
    }
    return new Block(valid_stmts);
  }

  int nstmts() const {
    return stmts_.size();
  }
  bool empty() const {
    return stmts_.empty();
  }

  void prepend_stmt(Stmt* s) {
    if (s->get_parent()) {
      throw malformed_input("Block prepend Stmt with existing parent", s);
    }

    stmts_.push_front(s);
    set_parent(s, this);
  }
  void append_stmt(Stmt* s) {
    if (s->get_parent()) {
      throw malformed_input("Block append Stmt with existing parent", s);
    }

    stmts_.push_back(s);
    set_parent(s, this);
  }

  void insert_stmt_before(Stmt* s, Stmt* before) {
    if (s->get_parent()) {
      throw malformed_input("Block append Stmt with existing parent", s);
    }

    auto pos = std::find(stmts_.begin(), stmts_.end(), before);
    if (pos == stmts_.end()) {
      throw malformed_input(
          "Inserting after statement that is not in block", s);
    }

    stmts_.insert(pos, s);
    set_parent(s, this);
  }

  void insert_stmt_after(Stmt* s, Stmt* after) {
    if (s->get_parent()) {
      throw malformed_input("Block append Stmt with existing parent", s);
    }

    auto pos = std::find(stmts_.begin(), stmts_.end(), after);
    if (pos == stmts_.end()) {
      throw malformed_input(
          "Inserting after statement that is not in block", s);
    }

    ++pos;

    stmts_.insert(pos, s);
    set_parent(s, this);
  }

  bool replace_stmt(Stmt* old_stmt, Stmt* new_stmt) {
    if (new_stmt->get_parent()) {
      throw malformed_input(
          "Block replace Stmt with existing parent", new_stmt);
    }

    auto pos = std::find(stmts_.begin(), stmts_.end(), old_stmt);
    if (pos == stmts_.end()) {
      return false;
    }
    stmts_.insert(pos, new_stmt);
    stmts_.erase(pos);
    set_parent(old_stmt, nullptr);
    set_parent(new_stmt, this);
    return true;
  }

  // Creates a new block by cloning `this` block and replacing the given
  // statement with a new statement. Note that `old_stmt` refers to a statement
  // in `this` block. If the `old_stmt` is not found, it will return `nullptr`.
  Block* clone_and_replace(Stmt* old_stmt, Stmt* new_stmt) {
    if (new_stmt->get_parent()) {
      throw malformed_input(
          "Block replace Stmt with existing parent", new_stmt);
    }

    // NOLINTNEXTLINE(cppcoreguidelines-init-variables)
    std::vector<Stmt*> stmts(stmts_.begin(), stmts_.end());
    // NOLINTNEXTLINE(cppcoreguidelines-init-variables)
    std::vector<Stmt*> cloned_stmts(stmts.size());
    bool found = false;
    for (int i = 0; i < static_cast<int>(stmts.size()); ++i) {
      if (stmts[i] == old_stmt) {
        found = true;
        cloned_stmts[i] = new_stmt;
      } else {
        cloned_stmts[i] = Stmt::clone(stmts[i]);
      }
    }
    if (!found) {
      return nullptr;
    }
    return new Block(cloned_stmts);
  }

  bool remove_stmt(Stmt* stmt) {
    auto pos = std::find(stmts_.begin(), stmts_.end(), stmt);
    if (pos == stmts_.end()) {
      return false;
    }

    set_parent(stmt, nullptr);
    stmts_.erase(pos);
    return true;
  }

  std::list<Stmt*> stmts() const {
    return stmts_;
  }

  void clear() {
    for (auto* s : stmts_) {
      set_parent(s, nullptr);
    }
    stmts_.clear();
  }

  void set_stmts(const std::vector<Stmt*>& stmts) {
    clear();
    init(stmts);
  }

  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
  explicit Block(const std::vector<Stmt*>& stmts) {
    init(stmts);
  }

  typedef std::list<Stmt*>::iterator iterator;
  typedef std::list<Stmt*>::const_iterator const_iterator;

  iterator begin() {
    return stmts_.begin();
  }

  const_iterator begin() const {
    return stmts_.begin();
  }

  iterator end() {
    return stmts_.end();
  }

  const_iterator end() const {
    return stmts_.end();
  }

  Stmt* front() {
    return stmts_.front();
  }

  Stmt* front() const {
    return stmts_.front();
  }

  Stmt* back() {
    return stmts_.back();
  }

  Stmt* back() const {
    return stmts_.back();
  }

  void splice(Block::iterator it, Block* other) {
    for (Stmt* s : *other) {
      set_parent(s, this);
    }

    stmts_.splice(it, other->stmts_);
  }

  static Block* getSharedParent(Stmt* p1, Stmt* p2) {
    // NOLINTNEXTLINE(cppcoreguidelines-init-variables)
    std::unordered_set<Block*> enclosing;

    Stmt* p1_p = p1;
    while (p1_p) {
      if (Block* b = dynamic_cast<Block*>(p1_p)) {
        if (b) {
          enclosing.insert(b);
        }
      }
      p1_p = p1_p->get_parent();
    }

    Stmt* p2_p = p2;
    while (p2_p) {
      if (Block* b = dynamic_cast<Block*>(p2_p)) {
        if (enclosing.count(b) != 0) {
          return b;
        }
      }
      p2_p = p2_p->get_parent();
    }

    return nullptr;
  }

  // returns the immediate child containing statement s.
  Stmt* getEnclosedRoot(Stmt* s) const {
    while (s && s->get_parent() != this) {
      s = s->get_parent();
    }
    return s;
  }

 private:
  std::list<Stmt*> stmts_;

  void init(const std::vector<Stmt*>& stmts) {
    for (Stmt* s : stmts) {
      if (!s) {
        continue;
      }
      if (!s->get_parent()) {
        // If we get here, it's a bug, but we cannot throw an error from a
        // constructor. But IR verifier would catch this.
        set_parent(s, this);
      }

      stmts_.push_back(s);
    }
  }
};

class TORCH_API Store : public StmtNode<Store> {
 public:
  Var* base_handle() const {
    return buf_->base_handle();
  }
  std::vector<Expr*> indices() const {
    return indices_;
  }
  Expr* flat_index() const {
    TORCH_CHECK(indices_.size() == 1, "Indices haven't been flattened.");
    return indices_[0];
  }
  Expr* value() const {
    return value_;
  }
  Buf* buf() const {
    return buf_;
  }

  void set_buf(Buf* buf) {
    buf_ = buf;
  }

  void set_indices(std::vector<Expr*> indices) {
    indices_ = std::move(indices);
  }

  void set_value(Expr* value) {
    value_ = value;
  }

  static Store* make(
      const BufHandle& buf,
      const std::vector<ExprHandle>& indices,
      const ExprHandle& value);

  Store(Buf* buf, std::vector<Expr*> indices, Expr* value);

 private:
  Buf* buf_;
  std::vector<Expr*> indices_;
  Expr* value_;
};

// Allocate a buffer of given shapes and dtypes and bind it with the given
// buffer var. The life span is at most through the current program, until it is
// explicitly freed. An unfreed memory is likely considered an error.
class TORCH_API Allocate : public StmtNode<Allocate> {
 public:
  static Allocate* make(const BufHandle& buf_handle) {
    return new Allocate(buf_handle.node());
  }

  Var* buffer_var() const {
    return buf_->base_handle();
  }

  Dtype dtype() const {
    return buf_->dtype();
  }

  const std::vector<Expr*> dims() const {
    return buf_->dims();
  }

  Buf* buf() const {
    return buf_;
  }

  void set_buf(Buf* buf) {
    buf_ = buf;
  }

  explicit Allocate(Buf* buf) : buf_(buf) {}

 private:
  Buf* buf_;
  // TODO: add memory types.
};

// Free the specific buffer. It is an error.
class TORCH_API Free : public StmtNode<Free> {
 public:
  static Free* make(const BufHandle& buf_handle) {
    return new Free(buf_handle.node());
  }

  Var* buffer_var() const {
    return buf_->base_handle();
  }

  Buf* buf() const {
    return buf_;
  }

  void set_buf(Buf* buf) {
    buf_ = buf;
  }

  explicit Free(Buf* buf) : buf_(buf) {}

 private:
  Buf* buf_;
};

class TORCH_API Let : public StmtNode<Let> {
 public:
  static Let* make(const VarHandle& var, const ExprHandle& val) {
    return new Let(var.node(), val.node());
  }

  Let(Var* var, Expr* val) : dtype_(var->dtype()), var_(var), val_(val) {}

  Dtype dtype() const {
    return dtype_;
  }

  Var* var() const {
    return var_;
  }

  Expr* value() const {
    return val_;
  }

  void set_var(Var* var) {
    var_ = var;
  }

  void set_val(Expr* val) {
    val_ = val;
  }

 private:
  Dtype dtype_;
  Var* var_;
  Expr* val_;
};

class TORCH_API Cond : public StmtNode<Cond> {
 public:
  static Cond* make(
      const ExprHandle& condition,
      Stmt* true_stmt,
      Stmt* false_stmt) {
    return new Cond(condition.node(), true_stmt, false_stmt);
  }

  Expr* condition() const {
    return condition_;
  }

  Block* true_stmt() const {
    return true_stmt_;
  }

  Block* false_stmt() const {
    return false_stmt_;
  }

  void set_condition(Expr* condition) {
    condition_ = condition;
  }

  void set_true_stmt(Stmt* true_stmt) {
    if (true_stmt) {
      Block* b = dynamic_cast<Block*>(true_stmt);
      if (!b) {
        b = new Block({true_stmt});
      }
      true_stmt_ = b;
      set_parent(true_stmt_, this);
    }
  }

  void set_false_stmt(Stmt* false_stmt) {
    if (false_stmt) {
      Block* b = dynamic_cast<Block*>(false_stmt);
      if (!b) {
        b = new Block({false_stmt});
      }
      false_stmt_ = b;
      set_parent(false_stmt_, this);
    }
  }

  Cond(Expr* condition, Stmt* true_stmt, Stmt* false_stmt)
      : condition_(condition) {
    set_true_stmt(true_stmt);
    set_false_stmt(false_stmt);
  }

  Cond* cloneWithNewBodies(Stmt* true_stmt, Stmt* false_stmt) {
    return new Cond(condition_, true_stmt, false_stmt);
  }

  Cond* cloneWithNewBody(Stmt* true_stmt) {
    return new Cond(condition_, true_stmt, nullptr);
  }

 private:
  Expr* condition_;
  Block* true_stmt_ = nullptr;
  Block* false_stmt_ = nullptr;
};

class TORCH_API LoopOptions {
 public:
  enum {
    IDX_UNSET = -1,
    IDX_X = 0,
    IDX_Y = 1,
    IDX_Z = 2,
    IDX_W = 3,
    IDX_MAX = IDX_W,
  };
  // GPU Block Index
  bool is_gpu_block_index() const {
    return gpu_block_index_ != IDX_UNSET;
  }

  int gpu_block_index() const {
    return gpu_block_index_;
  }

  std::string gpu_block_index_str() const {
    if (!is_gpu_block_index()) {
      throw malformed_input("Has no GPU block index");
    }

    // NOLINTNEXTLINE(modernize-avoid-c-arrays,cppcoreguidelines-avoid-c-arrays)
    static const char* kBlockIndexNames[] = {
        "blockIdx.x",
        "blockIdx.y",
        "blockIdx.z",
        "blockIdx.w",
    };

    if (gpu_block_index_ < IDX_X || gpu_block_index_ > IDX_MAX) {
      throw malformed_input("invalid GPU block index");
    }

    return kBlockIndexNames[gpu_block_index_];
  }

  void set_gpu_block_index(int index) {
    if (index == IDX_UNSET) {
      gpu_block_index_ = IDX_UNSET;
    }

    if (is_gpu_thread_index()) {
      throw std::runtime_error("Cannot set both gpu block and thread index");
    }
    if (is_gpu_block_index() && gpu_block_index() != index) {
      throw std::runtime_error("Cannot set a previously set block index");
    }
    gpu_block_index_ = index;
  }

  // GPU Thread Index
  bool is_gpu_thread_index() const {
    return gpu_thread_index() != IDX_UNSET;
  }

  int gpu_thread_index() const {
    return gpu_thread_index_;
  }

  std::string gpu_thread_index_str() const {
    if (!is_gpu_thread_index()) {
      throw malformed_input("has no GPU thread index");
    }

    // NOLINTNEXTLINE(modernize-avoid-c-arrays,cppcoreguidelines-avoid-c-arrays)
    static const char* kThreadIndexNames[] = {
        "threadIdx.x", "threadIdx.y", "threadIdx.z", "threadIdx.w"};

    if (gpu_thread_index_ < IDX_X || gpu_thread_index_ > IDX_MAX) {
      throw malformed_input("invalid GPU thread index");
    }

    return kThreadIndexNames[gpu_thread_index_];
  }

  void set_gpu_thread_index(int index) {
    if (index == IDX_UNSET) {
      gpu_thread_index_ = IDX_UNSET;
    }

    if (is_gpu_block_index()) {
      throw std::runtime_error("Cannot set both gpu thread and block index");
    }
    if (is_gpu_thread_index() && gpu_thread_index() != index) {
      throw std::runtime_error("Cannot set a previously set thread index");
    }
    gpu_thread_index_ = index;
  }

  void set_parallel() {
    is_parallel_ = true;
  }

  bool is_parallel() const {
    return is_parallel_;
  }

  std::string ToString() const {
    if (is_gpu_block_index()) {
      return gpu_block_index_str();
    } else if (is_gpu_thread_index()) {
      return gpu_thread_index_str();
    } else if (is_parallel()) {
      return "parallel";
    }
    return "";
  }

  bool isDefault() const {
    return gpu_block_index_ == IDX_UNSET && gpu_thread_index_ == IDX_UNSET &&
        !is_parallel_;
  }

  void set_buffer_mapping(const std::unordered_map<std::string, Buf*>& map) {
    map_input_to_tensor_bufs_ = map;
  }

  std::unordered_map<std::string, Buf*> get_buffer_mapping() const {
    return map_input_to_tensor_bufs_;
  }

 private:
  int gpu_block_index_{IDX_UNSET};
  int gpu_thread_index_{IDX_UNSET};
  bool is_parallel_{false};
  std::unordered_map<std::string, Buf*> map_input_to_tensor_bufs_;
};

class TORCH_API For : public StmtNode<For> {
 public:
  Var* var() const {
    return var_;
  }
  Expr* start() const {
    return start_;
  }
  Expr* stop() const {
    return stop_;
  }
  Block* body() const {
    return body_;
  }
  static For* make(
      const VarHandle& var,
      const ExprHandle& start,
      const ExprHandle& stop,
      Stmt* body) {
    if (!body) {
      return nullptr;
    }
    return new For(var.node(), start.node(), stop.node(), body);
  }
  static For* make(
      const VarHandle& var,
      const ExprHandle& start,
      const ExprHandle& stop,
      Stmt* body,
      const LoopOptions& loop_options) {
    if (!body) {
      return nullptr;
    }
    return new For(var.node(), start.node(), stop.node(), body, loop_options);
  }
  const LoopOptions loop_options() const {
    return loop_options_;
  }

  For(Var* var, Expr* start, Expr* stop, Stmt* body)
      : var_(var), start_(start), stop_(stop) {
    Block* b = dynamic_cast<Block*>(body);
    if (!b) {
      b = new Block({body});
    }
    body_ = b;
    set_parent(body_, this);
  }

  For(Var* var, Expr* start, Expr* stop, Stmt* body, LoopOptions loop_options)
      : var_(var),
        start_(start),
        stop_(stop),
        loop_options_(std::move(loop_options)) {
    if (!var) {
      throw malformed_input("invalid Var in For loop", var);
    } else if (!start) {
      throw malformed_input("invalid Start in For loop", start);
    } else if (!stop) {
      throw malformed_input("invalid Stop in For loop", stop);
    } else if (!body || body->get_parent()) {
      throw malformed_input("invalid Body in For loop", body);
    }

    Block* b = dynamic_cast<Block*>(body);
    if (!b) {
      b = new Block({body});
    }
    body_ = b;
    set_parent(body_, this);
  }

  void set_gpu_block_index(int block_index) {
    loop_options_.set_gpu_block_index(block_index);
  }

  void set_gpu_thread_index(int thread_index) {
    loop_options_.set_gpu_thread_index(thread_index);
  }

  void set_parallel() {
    loop_options_.set_parallel();
  }

  bool is_parallel() const {
    return loop_options_.is_parallel();
  }

  void set_buffer_map(const std::unordered_map<std::string, Buf*>& map) {
    loop_options_.set_buffer_mapping(map);
  }

  For* cloneWithNewBody(Stmt* body) const {
    return new For(var_, start_, stop_, body, loop_options_);
  }

  Block* removeBody() {
    auto res = body_;
    set_parent(res, nullptr);
    body_ = nullptr;
    return res;
  }

  void set_body(Stmt* body) {
    Block* b = dynamic_cast<Block*>(body);
    if (!b) {
      b = new Block({body});
    }
    body_ = b;
    set_parent(body_, this);
  }

  void set_start(Expr* start) {
    start_ = start;
  }

  void set_stop(Expr* stop) {
    stop_ = stop;
  }

  void set_var(Var* var) {
    var_ = var;
  }

 private:
  Var* var_;
  Expr* start_;
  Expr* stop_;
  Block* body_;
  LoopOptions loop_options_;
};

// A backend specific IR Node that implements atomic-add.
// This node could only shows up as an internal with GPU backends.
// TODO: move to this an internal IR.
// TODO: make IR nodes extensible.
class TORCH_API AtomicAdd : public StmtNode<AtomicAdd> {
 public:
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
  AtomicAdd(Buf* buf, std::vector<Expr*> indices, Expr* value)
      : buf_(buf), indices_(std::move(indices)), value_(value) {}

  Var* base_handle() const {
    return buf_->base_handle();
  }

  Buf* buf() const {
    return buf_;
  }

  Expr* flat_index() const {
    TORCH_CHECK(indices_.size() == 1, "Indices haven't been flattened.");
    return indices_[0];
  }

  Expr* value() const {
    return value_;
  }

  const std::vector<Expr*>& indices() const {
    return indices_;
  }

  void set_buf(Buf* buf) {
    buf_ = buf;
  }

  void set_indices(std::vector<Expr*> indices) {
    indices_ = std::move(indices);
  }

  void set_value(Expr* value) {
    value_ = value;
  }

 private:
  Buf* buf_;
  std::vector<Expr*> indices_;
  Expr* value_;
};

class TORCH_API SyncThreads : public StmtNode<SyncThreads> {
 public:
  SyncThreads() = default;
};

/*
 * ExternalCall statement represents a call to an external function that would
 * compute the contents of the output buffer. An ExternalCall statement consists
 * of:
 *   1) output buffer - the buffer that'll be initialized by the call
 *   2) external function name - a key from the NNC function registry to lookup
 *      the actual function to call
 *   3) buffer arguments - the input buffers used by the function
 *   4) non-buffer arguments - scalar arguments to pass to the function
 *
 * An example:
 *   A = nnc_conv2d(buf_args={Input, Weight, Bias}, args={1})
 * Here 'A' is the output buffer, "nnc_conv2d" is the function name, the buffer
 * arguments are 'Input', 'Weight', and 'Bias', and there is a single non-buffer
 * argument - 1.
 *
 * The semantics of the scalar arguments is defined solely by the implementation
 * of the external function.
 */
class TORCH_API ExternalCall : public StmtNode<ExternalCall> {
 public:
  static ExternalCall* make(
      BufHandle buf,
      const std::string& func_name,
      const std::vector<BufHandle>& buf_args,
      const std::vector<ExprHandle>& args);

  Buf* buf() const {
    return buf_;
  }

  std::string func_name() const {
    return func_name_;
  }

  std::vector<Buf*> buf_args() const {
    return buf_args_;
  }

  std::vector<Expr*> args() const {
    return args_;
  }

  void set_buf(Buf* buf) {
    buf_ = buf;
  }

  void set_buf_args(std::vector<Buf*> buf_args) {
    buf_args_ = std::move(buf_args);
  }

  void set_args(std::vector<Expr*> args) {
    args_ = std::move(args);
  }

  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
  ExternalCall(
      Buf* buf,
      std::string func_name,
      std::vector<Buf*> buf_args,
      std::vector<Expr*> args)
      : buf_(buf),
        func_name_(std::move(func_name)),
        buf_args_(std::move(buf_args)),
        args_(std::move(args)) {}

 private:
  Buf* buf_;
  std::string func_name_;
  std::vector<Buf*> buf_args_;
  std::vector<Expr*> args_;
};

} // namespace tensorexpr
} // namespace jit
} // namespace torch
