/*-
 * Copyright (c) 2011 NetApp, Inc.
 * Copyright (c) 2017 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY NETAPP, INC ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL NETAPP, INC OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef VLAPIC_H
#define VLAPIC_H

/*
 * 16 priority levels with at most one vector injected per level.
 */
#define	ISRVEC_STK_SIZE		(16U + 1U)

#define VLAPIC_MAXLVT_INDEX	APIC_LVT_CMCI

struct vlapic_pir_desc {
	uint64_t pir[4];
	uint64_t pending;
	uint64_t unused[3];
} __aligned(64);

struct vlapic_timer {
	struct hv_timer timer;
	uint32_t mode;
	uint32_t tmicr;
	uint32_t divisor_shift;
};

struct acrn_vlapic {
	/*
	 * Please keep 'apic_page' and 'pir_desc' be the first two fields in
	 * current structure, as below alignment restrictions are mandatory
	 * to support APICv features:
	 * - 'apic_page' MUST be 4KB aligned.
	 * - 'pir_desc' MUST be 64 bytes aligned.
	 */
	struct lapic_regs	apic_page;
	struct vlapic_pir_desc	pir_desc;

	struct vm		*vm;
	struct vcpu		*vcpu;

	uint32_t		esr_pending;
	int			esr_firing;

	struct vlapic_timer	vtimer;

	/*
	 * The 'isrvec_stk' is a stack of vectors injected by the local apic.
	 * A vector is popped from the stack when the processor does an EOI.
	 * The vector on the top of the stack is used to compute the
	 * Processor Priority in conjunction with the TPR.
	 *
	 * Note: isrvec_stk_top is unsigned and always equal to the number of
	 * vectors in the stack.
	 *
	 * Operations:
	 *     init: isrvec_stk_top = 0;
	 *     push: isrvec_stk_top++; isrvec_stk[isrvec_stk_top] = x;
	 *     pop : isrvec_stk_top--;
	 */
	uint8_t		isrvec_stk[ISRVEC_STK_SIZE];
	uint32_t	isrvec_stk_top;

	uint64_t	msr_apicbase;

	/*
	 * Copies of some registers in the virtual APIC page. We do this for
	 * a couple of different reasons:
	 * - to be able to detect what changed (e.g. svr_last)
	 * - to maintain a coherent snapshot of the register (e.g. lvt_last)
	 */
	uint32_t	svr_last;
	uint32_t	lvt_last[VLAPIC_MAXLVT_INDEX + 1];
} __aligned(CPU_PAGE_SIZE);


/* APIC write handlers */
void vlapic_set_cr8(struct acrn_vlapic *vlapic, uint64_t val);
uint64_t vlapic_get_cr8(struct acrn_vlapic *vlapic);

/*
 * Returns 0 if there is no eligible vector that can be delivered to the
 * guest at this time and non-zero otherwise.
 *
 * If an eligible vector number is found and 'vecptr' is not NULL then it will
 * be stored in the location pointed to by 'vecptr'.
 *
 * Note that the vector does not automatically transition to the ISR as a
 * result of calling this function.
 */
int vlapic_pending_intr(struct acrn_vlapic *vlapic, uint32_t *vecptr);

/*
 * Transition 'vector' from IRR to ISR. This function is called with the
 * vector returned by 'vlapic_pending_intr()' when the guest is able to
 * accept this interrupt (i.e. RFLAGS.IF = 1 and no conditions exist that
 * block interrupt delivery).
 */
void vlapic_intr_accepted(struct acrn_vlapic *vlapic, uint32_t vector);
void vlapic_post_intr(uint16_t dest_pcpu_id);
uint64_t apicv_get_pir_desc_paddr(struct vcpu *vcpu);

struct acrn_vlapic *vm_lapic_from_pcpuid(struct vm *vm, uint16_t pcpu_id);
int vlapic_rdmsr(struct vcpu *vcpu, uint32_t msr, uint64_t *rval);
int vlapic_wrmsr(struct vcpu *vcpu, uint32_t msr, uint64_t wval);

/*
 * Signals to the LAPIC that an interrupt at 'vector' needs to be generated
 * to the 'cpu', the state is recorded in IRR.
 */
int vlapic_set_intr(struct vcpu *vcpu, uint32_t vector, bool level);

#define	LAPIC_TRIG_LEVEL	true
#define	LAPIC_TRIG_EDGE		false
static inline int
vlapic_intr_level(struct vcpu *vcpu, uint32_t vector)
{
	return vlapic_set_intr(vcpu, vector, LAPIC_TRIG_LEVEL);
}

static inline int
vlapic_intr_edge(struct vcpu *vcpu, uint32_t vector)
{
	return vlapic_set_intr(vcpu, vector, LAPIC_TRIG_EDGE);
}

/*
 * Triggers the LAPIC local interrupt (LVT) 'vector' on 'cpu'.  'cpu' can
 * be set to -1 to trigger the interrupt on all CPUs.
 */
int vlapic_set_local_intr(struct vm *vm, uint16_t vcpu_id_arg, uint32_t vector);

int vlapic_intr_msi(struct vm *vm, uint64_t addr, uint64_t msg);

void vlapic_deliver_intr(struct vm *vm, bool level, uint32_t dest,
		bool phys, uint32_t delmode, uint32_t vec, bool rh);

/* Reset the trigger-mode bits for all vectors to be edge-triggered */
void vlapic_reset_tmr(struct acrn_vlapic *vlapic);

/*
 * Set the trigger-mode bit associated with 'vector' to level-triggered if
 * the (dest,phys,delmode) tuple resolves to an interrupt being delivered to
 * this 'vlapic'.
 */
void vlapic_set_tmr_one_vec(struct acrn_vlapic *vlapic, uint32_t delmode,
		uint32_t vector, bool level);

void vlapic_apicv_batch_set_tmr(struct acrn_vlapic *vlapic);
uint32_t vlapic_get_id(struct acrn_vlapic *vlapic);
uint8_t vlapic_get_apicid(struct acrn_vlapic *vlapic);
int vlapic_create(struct vcpu *vcpu);
/*
 *  @pre vcpu != NULL
 */
void vlapic_free(struct vcpu *vcpu);
void vlapic_init(struct acrn_vlapic *vlapic);
void vlapic_reset(struct acrn_vlapic *vlapic);
void vlapic_restore(struct acrn_vlapic *vlapic, struct lapic_regs *regs);
bool vlapic_enabled(struct acrn_vlapic *vlapic);
uint64_t vlapic_apicv_get_apic_access_addr(void);
uint64_t vlapic_apicv_get_apic_page_addr(struct acrn_vlapic *vlapic);
void vlapic_apicv_inject_pir(struct acrn_vlapic *vlapic);
int apic_access_vmexit_handler(struct vcpu *vcpu);
int apic_write_vmexit_handler(struct vcpu *vcpu);
int veoi_vmexit_handler(struct vcpu *vcpu);
int tpr_below_threshold_vmexit_handler(__unused struct vcpu *vcpu);
void calcvdest(struct vm *vm, uint64_t *dmask, uint32_t dest, bool phys);
#endif /* VLAPIC_H */
