// Copyright 2010 Google Inc.
// 
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
//      http://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
// ------------------------------------------------------------------------

#include "engine/globals.h"
#include "public/commandlineflags.h"
#include "public/logging.h"

#include "engine/memory.h"
#include "engine/utils.h"
#include "engine/opcode.h"
#include "engine/scope.h"
#include "engine/type.h"
#include "engine/node.h"
#include "engine/analyzer.h"
#include "engine/symboltable.h"
#include "engine/proc.h"


namespace sawzall {


// This flag allows disabling of precise analysis of composite
// expressions without rolling back the compiler.
// TODO: Remove the flag when we're confident the analysis
// is working.
DEFINE_bool(deep_composite_fields,
            false,
            "all fields of composite expressions will be considered read");

// ----------------------------------------------------------------------------
//  Visit the AST to determine which fields of tuples are referenced.
//  This analysis is separated from the parser, so that we can perform
//  it after other optimizations, such as eliminating unreachable functions.
//
//  The --read_all_fields flag will override this analysis by ignoring
//  the results that it computes.
//
//  The rules for setting the read bits of tuple fields are:
//
//  An individual field is read when:
//    * a selector expression is used anywhere besides the last Node
//      on the left-hand-side of an assignment
//
//  The top level fields of a tuple (but not nested fields) are read when:
//    * an array is converted to a tuple
//    * a type is inferred for an anonymous tuple (e.g. in a return statement)
//    * a composite is assigned to a tuple
//
//  All of the fields of a tuple (including nested tuple fields) are read when:
//    * a tuple is predefined
//    * a tuple is tested for equality
//    * a tuple is an index, element or weight of an output type used in an
//      emit statement
//    * a tuple is passed to a generic intrinsic or fingerprintof
//    * a tuple is the source type of a conversion
//    * a tuple is used as the key of a map
// ----------------------------------------------------------------------------


class FieldReferenceAnalysis {
 public:
  // External interface.
  static void ComputeReferencedFields(Analyzer* analyzer) {
    FieldReferenceAnalysis field_ref(analyzer);
    field_ref.SetFieldReferenceImpl();
  }

  static void EliminateDeadAssignments(Analyzer* analyzer) {
    FieldReferenceAnalysis field_ref(analyzer);
    field_ref.EliminateDeadAssignmentImpl();
  }

 private:
  // Private class to visit nodes and determine which tuple fields are read.
  class SetReferencedVisitor : public DeepNodeVisitor {
   public:
    SetReferencedVisitor(FieldReferenceAnalysis* outer) : outer_(outer),
                                                          is_lhs_(false) { }

    void SetReferences();
   private:
    // For most nodes just visit the child nodes as rvalues.
    virtual void DoNode(Node* x)  {
      is_lhs_ = false;
      x->VisitChildren(this);
    }

    // Set the lhs flag when visiting the left hand side.
    virtual void DoAssignment(Assignment* x);

    // Set the read bits for comparison operators.
    virtual void DoBinary(Binary* x);

    // Set the read bits for function calls (e.g. fingerprint intrinsic)
    virtual void DoCall(Call* x);

    // Set the read bits for composites interpreted as tuples.
    virtual void DoComposite(Composite* x);

    // Set the read bits for tuple conversions.
    virtual void DoConversion(Conversion* x);

    // Set the read bits for fields that are accessed.
    virtual void DoSelector(Selector* x);

    // For most types just visit the child types
    virtual void DoType(Type* x)  { x->VisitChildren(this); }

    // Set read bits of tuples and fields used by an OutputType.
    virtual void DoOutputType(OutputType* x);

    Analyzer* analyzer() { return outer_->analyzer(); }

    FieldReferenceAnalysis* outer_;   // enclosing instance
    bool is_lhs_;                     // to distinguish writes from reads
  };

  // Constructor; only static methods like ComputeReferencedFields and
  // EliminateDeadAssignments may use instances.
  FieldReferenceAnalysis(Analyzer* analyzer) :  analyzer_(analyzer) { }

  // Compute field read bits based on the current version of the program.
  void SetFieldReferenceImpl();

  // Clear read bits in all fields of tuple types.  Use before
  // re-running field reference analysis.
  void ClearReferences();

  // Eliminate assignments to write-only fields.
  void EliminateDeadAssignmentImpl();

  Analyzer* analyzer() { return analyzer_; }

  Analyzer* analyzer_;
};


void FieldReferenceAnalysis::SetFieldReferenceImpl() {
  // Process tuple types before visiting the code.  Some tuples have
  // no TypDecl to be visited.
  const List<TupleType*>* tuple_types = analyzer_->proc()->GetTupleTypes();
  if (tuple_types != NULL) {
    for (int i = 0; i < tuple_types->length(); i++) {
      TupleType* tuple_type = tuple_types->at(i);
      if (tuple_type->is_predefined() || tuple_type->tested_for_equality()) {
        tuple_type->SetAllFieldsRead(true);
      }
    }
  }
  SetReferencedVisitor setter(this);
  setter.SetReferences();
}


// Clears information from previous pass, in case we re-run
// FieldReferenceAnalysis.
void FieldReferenceAnalysis::ClearReferences() {
  const List<TupleType*>* tuple_types = analyzer_->proc()->GetTupleTypes();
  if (tuple_types != NULL) {
    for (int i = 0; i < tuple_types->length(); i++) {
      tuple_types->at(i)->ClearAllFieldsRead();
    }
  }
}


// ----------------------------------------------------------------------------
//  Set field reference node visitor implementation.
// ----------------------------------------------------------------------------

void FieldReferenceAnalysis::SetReferencedVisitor::SetReferences() {
  SymbolTable* symbol_table = analyzer()->symbol_table();
  symbol_table->program()->Visit(this);
  // Static initializers may contain field references.
  for (int i = 0; i < symbol_table->statics()->length(); i++) {
    symbol_table->statics()->at(i)->Visit(this);
  }
}


void FieldReferenceAnalysis::SetReferencedVisitor::DoAssignment(Assignment* x) {
  is_lhs_ = true;
  x->lvalue()->Visit(this);

  is_lhs_ = false;
  x->rvalue()->Visit(this);
}


void FieldReferenceAnalysis::SetReferencedVisitor::DoBinary(Binary* x) {
  assert(!is_lhs_);
  DoNode(x);

  if (x->op() == Binary::EQL || x->op() == Binary::NEQ) {
    // Comparison of values causes all fields to be read.
    x->left()->type()->SetAllFieldsRead(true);
    x->right()->type()->SetAllFieldsRead(true);
  }
}


void FieldReferenceAnalysis::SetReferencedVisitor::DoCall(Call* x) {
  assert(!is_lhs_);
  DoNode(x);

  Intrinsic* intrinsic = x->fun()->AsIntrinsic();
  if (intrinsic != NULL && (intrinsic->kind() == Intrinsic::INTRINSIC ||
                            intrinsic->kind() == Intrinsic::FINGERPRINTOF)) {
    for (int i = 0; i < x->args()->length(); i++) {
      Type* arg_type = x->args()->at(i)->type();
      arg_type->SetAllFieldsRead(true);
    }
  }
}


void FieldReferenceAnalysis::SetReferencedVisitor::DoComposite(Composite* x) {
  assert(!is_lhs_);
  DoNode(x);

  // Handle cases such as an array value converted to a tuple.
  TupleType* ttype = x->type()->as_tuple();
  if (ttype != NULL) {
    // would also set all fields of ttype written if tracking writes
    ttype->SetAllFieldsRead(FLAGS_deep_composite_fields);
  }
}


void FieldReferenceAnalysis::SetReferencedVisitor::DoConversion(Conversion* x) {
  assert(!is_lhs_);
  DoNode(x);

  // A conversion from a type containing a tuple causes all of the tuple's
  // fields to be read.
  // We would also set fields of the destination type written if separately
  // tracking writes.
  x->src()->type()->SetAllFieldsRead(true);
}


void FieldReferenceAnalysis::SetReferencedVisitor::DoSelector(Selector* x) {
  if (!is_lhs_) {
    x->field()->set_read();
  }

  is_lhs_ = false;
  x->VisitChildren(this);
}


void FieldReferenceAnalysis::SetReferencedVisitor::DoOutputType(OutputType* x) {
  assert(!is_lhs_);
  DeepNodeVisitor::DoOutputType(x);
  if (x->index_decls() != NULL) {
    for (int i = 0; i < x->index_decls()->length(); i++) {
      VarDecl* index = x->index_decls()->at(i);
      index->type()->SetAllFieldsRead(true);
    }
  }
  if (x->elem_decl() != NULL) {
    x->elem_decl()->type()->SetAllFieldsRead(true);
  }
  if (x->weight() != NULL) {
    x->weight()->set_read();
    x->weight()->type()->SetAllFieldsRead(true);
  }
}


// ----------------------------------------------------------------------------
//  Analyzer interface to function value propagation.
// ----------------------------------------------------------------------------

void Analyzer::SetReferencedFields() {
  // Set new field read bits.
  FieldReferenceAnalysis::ComputeReferencedFields(this);
}


}  // namespace sawzall
