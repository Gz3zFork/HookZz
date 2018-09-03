#include "srcxx/InterceptRouting.h"
#include "srcxx/Interceptor.h"
#include "hookzz_internal.h"

#include "vm_core/modules/codegen/codegen-arm64.h"

#define ARM64_TINY_REDIRECT_SIZE 4
#define ARM64_FULL_REDIRECT_SIZE 16
#define ARM64_NEAR_JUMP_RANGE ((1 << 25) << 2)

void InterceptRouting::Prepare() {
  uint64_t src_pc          = (uint64_t)entry_->target_address;
  int need_relocated_size  = ARM64_FULL_REDIRECT_SIZE;
  Interceptor *interceptor = Interceptor::SharedInstance();
  if (interceptor->options().enable_b_branch) {
    DLOG("Enable b branch maybe cause crash, if crashed, please disable it.\n");
    need_relocated_size = ARM64_TINY_REDIRECT_SIZE;
  }

  // save original prologue
  memcpy(entry_->origin_instructions.data, entry_->target_address, need_relocated_size);
  entry_->origin_instructions.size    = need_relocated_size;
  entry_->origin_instructions.address = entry_->target_address;
}

void InterceptRouting::BuildPreCallRouting() {
  // create closure trampoline jump to prologue_routing_dispath with the `entry_` data
  ClosureTrampolineEntry *closure_trampoline_entry =
      ClosureTrampoline::CreateClosureTrampoline(entry_, prologue_routing_dispatch);
  entry_->prologue_dispatch_bridge = closure_trampoline_entry->address;

  Interceptor *interceptor = Interceptor::SharedInstance();
  if (interceptor->options().enable_b_branch) {
    BuildFastForwardTrampoline();
  }
  DLOG("create pre call closure trampoline to %p\n", closure_trampoline_entry->address);
}

void InterceptRouting::BuildFastForwardTrampoline() {
  TurboAssembler *turbo_assembler_;
#define _ turbo_assembler_
  CodeGen codegen = CodeGen(turbo_assembler_);
  if (branch_type_ == kFunctionInlineHook) {
    codegen.LiteralBrBranch(entry_->replace_call);
  } else if (branch_type_ == kDynamicBinaryInstrumentation) {
    codegen.LiteralBrBranch(entry_->dynamic_binary_instrumentation_bridge);
  }
  coedgen.Commit();
}

void ARM64InterceptorBackend::BuildForEnter(HookEntry *entry) {
  ARM64HookEntryBackend *entry_backend = (ARM64HookEntryBackend *)entry->backend;
  RetStatus status                     = RS_SUCCESS;

  if (entry->hook_type == HOOK_TYPE_FUNCTION_via_GOT) {
    DynamicClosureTrampoline *dcbInfo;
    DynamicClosureBridge *dcb = Singleton<DynamicClosureBridge>::GetInstance();
    dcbInfo = dcb->allocateDynamicClosureBridge((void *)entry, (void *)dynamic_context_begin_invocation_bridge_handler);
    if (dcbInfo == NULL) {
      ERROR_LOG_STR("build closure bridge failed!!!");
    }
    entry->on_enter_trampoline = dcbInfo->address;
  } else {
    ClosureTrampolineEntry *entry;
    ClosureBridge *cb = Singleton<ClosureBridge>::GetInstance();
    entry             = cb->CreateClosureTrampoline(entry, (void *)context_begin_invocation_bridge_handler);
    if (entry == NULL) {
      ERROR_LOG_STR("build closure bridge failed!!!");
    }
    entry->on_enter_trampoline = entry->address;
  }

  // build the double trampline aka enter_transfer_trampoline
  if (entry_backend && entry_backend->limit_relocate_inst_size == ARM64_TINY_REDIRECT_SIZE) {
    if (entry->hook_type != HOOK_TYPE_FUNCTION_via_GOT) {
      BuildForEnterTransfer(entry);
    }
  }
}

void ARM64InterceptorBackend::BuildForEnterTransfer(HookEntry *entry) {
  ARM64HookEntryBackend *entry_backend = (ARM64HookEntryBackend *)entry->backend;
  RetStatus status                     = RS_SUCCESS;

  relocatorARM64->output->reset(0);

  relocatorARM64->output->put_ldr_reg_imm(ARM64_REG_X17, 0x8);
  relocatorARM64->output->put_br_reg(ARM64_REG_X17);
  if (entry->hook_type == HOOK_TYPE_FUNCTION_via_REPLACE) {
    relocatorARM64->output->putBytes(&entry->replace_call, sizeof(void *));
  } else if (entry->hook_type == HOOK_TYPE_INSTRUCTION_via_DBI) {
    relocatorARM64->output->putBytes(&entry->on_dynamic_binary_instrumentation_trampoline, sizeof(void *));
  } else {
    relocatorARM64->output->putBytes(&entry->on_enter_trampoline, sizeof(void *));
  }

  if (entry_backend->limit_relocate_inst_size == ARM64_TINY_REDIRECT_SIZE) {
    MemoryManager *memory_manager = Singleton<MemoryManager>::GetInstance();
    CodeCave *cc                  = memory_manager->searchNearCodeCave(entry->target_address, ARM64_NEAR_JUMP_RANGE,
                                                      relocatorARM64->output->instBytes.size());
    relocatorARM64->output->NearPatchTo((void *)cc->address, ARM64_NEAR_JUMP_RANGE);
    entry->on_enter_transfer_trampoline = (void *)cc->address;
    delete (cc);
  } else {
    MemoryManager *memory_manager = Singleton<MemoryManager>::GetInstance();
    CodeSlice *cs                 = memory_manager->allocateCodeSlice(relocatorARM64->output->instBytes.size());
    relocatorARM64->output->PatchTo(cs->data);
    entry->on_enter_transfer_trampoline = cs->data;
    delete (cs);
  }
}

void ARM64InterceptorBackend::BuildForDynamicBinaryInstrumentation(HookEntry *entry) {
  ARM64HookEntryBackend *entry_backend = (ARM64HookEntryBackend *)entry->backend;
  ClosureTrampolineEntry *entry;
  ClosureBridge *cb = Singleton<ClosureBridge>::GetInstance();
  entry             = cb->CreateClosureTrampoline(entry, (void *)dynamic_binary_instrumentationn_bridge_handler);
  if (entry == NULL) {
    ERROR_LOG_STR("build closure bridge failed!!!");
  }

  entry->on_dynamic_binary_instrumentation_trampoline = entry->address;

  // build the double trampline aka enter_transfer_trampoline
  if (entry_backend->limit_relocate_inst_size == ARM64_TINY_REDIRECT_SIZE) {
    if (entry->hook_type != HOOK_TYPE_FUNCTION_via_GOT) {
      BuildForEnterTransfer(entry);
    }
  }
}

void ARM64InterceptorBackend::BuildForLeave(HookEntry *entry) {
  ARM64HookEntryBackend *entry_backend = (ARM64HookEntryBackend *)entry->backend;

  if (entry->hook_type == HOOK_TYPE_FUNCTION_via_GOT) {
    DynamicClosureTrampoline *dcbInfo;
    DynamicClosureBridge *dcb = Singleton<DynamicClosureBridge>::GetInstance();
    dcbInfo = dcb->allocateDynamicClosureBridge(entry, (void *)dynamic_context_end_invocation_bridge_handler);
    if (dcbInfo == NULL) {
      ERROR_LOG_STR("build closure bridge failed!!!");
    }
    entry->on_leave_trampoline = dcbInfo->address;
  } else {
    ClosureTrampolineEntry *entry;
    ClosureBridge *cb = Singleton<ClosureBridge>::GetInstance();
    entry             = cb->CreateClosureTrampoline(entry, (void *)context_end_invocation_bridge_handler);
    if (entry == NULL) {
      ERROR_LOG_STR("build closure bridge failed!!!");
    }
    entry->on_leave_trampoline = entry->address;
  }
}

void ARM64InterceptorBackend::BuildForInvoke(HookEntry *entry) {
  ARM64HookEntryBackend *entry_backend = (ARM64HookEntryBackend *)entry->backend;
  RetStatus status                     = RS_SUCCESS;

  zz_addr_t originNextInstAddress;

  relocatorARM64->input->reset(entry->target_address, entry->target_address);
  relocatorARM64->output->reset(0);

  relocatorARM64->reset();

  do {
    relocatorARM64->relocateWrite();
  } while (relocatorARM64->indexRelocatedInputOutput.size() < relocatorARM64->input->instCTXs.size());

  assert(entry_backend->limit_relocate_inst_size == relocatorARM64->input->instCTXs.size());
  originNextInstAddress = (zz_addr_t)entry->target_address + relocatorARM64->input->instCTXs.size();

  relocatorARM64->output->put_ldr_reg_imm(ARM64_REG_X17, 0x8);
  relocatorARM64->output->put_br_reg(ARM64_REG_X17);
  relocatorARM64->output->putBytes(&originNextInstAddress, sizeof(void *));

  MemoryManager *memory_manager = MemoryManager::GetInstance();
  CodeSlice *cs                 = memory_manager->allocateCodeSlice(relocatorARM64->output->instBytes.size());
  relocatorARM64->output->RelocatePatchTo(relocatorARM64, cs->data);
  entry->on_invoke_trampoline = cs->data;
  delete (cs);

  // debug log
  if (1) {
    char buffer[1024]         = {};
    char origin_prologue[256] = {0};
    int t                     = 0;
    sprintf(buffer + strlen(buffer), "\n======= ARM64 Invoke Logging ======= \n");
    for (int i = 0; i < relocatorARM64->input->instCTXs.size(); i++) {
      sprintf(origin_prologue + t, "0x%.2x ", relocatorARM64->input->instCTXs[i]->bytes);
    }
    sprintf(buffer + strlen(buffer), "\tARM Origin Prologue: %s\n", origin_prologue);
    sprintf(buffer + strlen(buffer), "\tInput Address: %p\n", relocatorARM64->input->buffer);
    sprintf(buffer + strlen(buffer), "\tInput Instruction Count: %lu\n", relocatorARM64->input->instCTXs.size());
    sprintf(buffer + strlen(buffer), "\tInput Instruction ByteSize: %lu\n", relocatorARM64->input->instBytes.size());
    sprintf(buffer + strlen(buffer), "\tOutput Address: %p\n", entry->on_invoke_trampoline);
    sprintf(buffer + strlen(buffer), "\tOutput Instruction Count: %lu\n", relocatorARM64->output->instCTXs.size());
    sprintf(buffer + strlen(buffer), "\tOutput Instruction ByteSize: %lu\n", relocatorARM64->output->instBytes.size());

    for (auto it : relocatorARM64->indexRelocatedInputOutput) {
      sprintf(buffer + strlen(buffer), "\t\tinput(%p) -> relocated ouput(%p), relocate %d instruction\n",
              (void *)(relocatorARM64->input->instCTXs[it.first]->address),
              (void *)(relocatorARM64->output->instCTXs[it.second]->address), 0);
    }
    DEBUGLOG_COMMON_LOG("%s", buffer);
  }
}

void ARM64InterceptorBackend::ActiveTrampoline(HookEntry *entry) {
  ARM64HookEntryBackend *entry_backend = (ARM64HookEntryBackend *)entry->backend;
  RetStatus status                     = RS_SUCCESS;

  if (entry->hook_type == HOOK_TYPE_FUNCTION_via_REPLACE) {
    if (entry_backend->limit_relocate_inst_size == ARM64_TINY_REDIRECT_SIZE) {
      relocatorARM64->output->put_b_imm((zz_addr_t)entry->on_enter_transfer_trampoline -
                                        (zz_addr_t)relocatorARM64->output->pc);
    } else {
      relocatorARM64->output->put_ldr_reg_imm(ARM64_REG_X17, 0x8);
      relocatorARM64->output->put_br_reg(ARM64_REG_X17);
      relocatorARM64->output->putBytes(&entry->on_enter_transfer_trampoline, sizeof(void *));
    }
  } else {
    if (entry_backend->limit_relocate_inst_size == ARM64_TINY_REDIRECT_SIZE) {
      relocatorARM64->output->put_b_imm((zz_addr_t)entry->on_enter_transfer_trampoline -
                                        (zz_addr_t)relocatorARM64->output->pc);
    } else {
      relocatorARM64->output->put_ldr_reg_imm(ARM64_REG_X17, 0x8);
      relocatorARM64->output->put_br_reg(ARM64_REG_X17);
      relocatorARM64->output->putBytes(&entry->on_enter_trampoline, sizeof(void *));
    }
  }
}