/*********************************************************************
 *
 * (C) Copyright IBM Corp. 2007,2010
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses>.
 *
 ********************************************************************/


#ifndef _BGP_SPI_LINUX_INTERFACE_H_  /*  Prevent multiple inclusion */
#define _BGP_SPI_LINUX_INTERFACE_H_


/*! \brief Returns the physical processor ID of the running PPC450 core.
 *
 * \return Physical processor ID
 * \retval 0 Running on processor 0
 * \retval 1 Running on processor 1
 * \retval 2 Running on processor 2
 * \retval 3 Running on processor 3
 */
extern inline uint32_t Kernel_PhysicalProcessorID( void ) { return 0; }  /*  ?????? */


/*! \brief Causes a commthread to disappear from the runqueue
 *
 *  \note Kernel does not guarantee that the instruction pointer, stack pointer, and register state are preserved across a poof.
 *  \note TLS data is preserved across a poof
 *  \note This SPI is only executable on a comm. thread.
 *  \warning non-portable pthread API
 *  \return error indication
 *  \retval success Does not return.  Thread has "poofed"
 *  \retval -1 Calling thread is not a CommThread, so cannot poof
 */
int pthread_poof_np( void );




/*!
 * \brief Clears the Full Reception FIFO (DD1 workaround)
 *
 * This function exists to reset the DMA reception fifos - it is a workaround for DD1 only.  It should not be needed in DD2.
 *
 * \retval  0            Successful
 * \retval  error_value  An error value defined in the _BGP_RAS_DMA_ErrCodes
 *                       enum located in bgp/arch/include/common/bgp_ras.h
 *
 */
int Kernel_ClearFullReceptionFifo(void);


/*! \brief Generates an InterruptID value
 * \param[in] group group of the interrupt.  range 0-9.
 * \param[in] irq_in_group irq within the group.  range 0-31.
 * \return Composite value able to be passed to Kernel_SetCommThreadConfig
 * \see Kernel_SetCommThreadConfig
 */
#define Kernel_MkInterruptID(group, irq_in_group) ((group<<5)|(irq_in_group&0x1f))


/*!
 * \brief Communication Thread interrupt handler function prototype
 *
 * \param[in] arg1 1st argument to commthread
 * \param[in] arg2 2nd argument to commthread
 * \param[in] arg3 3rd argument to commthread
 */
typedef void (*Kernel_CommThreadHandler)(uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);

/*!
 * \brief Interrupt Group Prototype
 *
 * This data type is used to group interrupts of various devices together
 * so they can be enabled or disabled simultaneously.  A given interrupt user
 * (eg. messaging, QCD, etc) specifies a value of this data type when its
 * interrupt resources are allocated.  The kernel associates those resources
 * with the specified value so when this value is specified on the enable or
 * disable interupts system call, all of the interrupts in the group are
 * operated upon.  Examples of devices that can be grouped in this way include
 * DMA fifos, torus, tree, etc.
 *
 * \todo The kernel should provide interfaces to allocate a
 *       Kernel_InterruptGroup_t and deallocate it.
 */
typedef void * Kernel_InterruptGroup_t;


/*! \defgroup COMMTHRD_OPCODES CommThread Opcodes
 *  \{
 * \note Only 1 interrupt route can be specified per opcode
 * \note CallFunc, DisableIntOnEntry, EnableIntOnPoof can be specified in any combination
 * \note Current support requires that DisableIntOnEntry and EnableIntOnPoof be specified
 */
#define COMMTHRD_OPCODE_DISABLE            0x00  /* !< Interrupt route - Not routed / interrupt disabled */
#define COMMTHRD_OPCODE_CORE0              0x01  /* !< Interrupt route - Dispatched on core0 */
#define COMMTHRD_OPCODE_CORE1              0x02  /* !< Interrupt route - Dispatched on core1 */
#define COMMTHRD_OPCODE_CORE2              0x03  /* !< Interrupt route - Dispatched on core2 */
#define COMMTHRD_OPCODE_CORE3              0x04  /* !< Interrupt route - Dispatched on core3 */
#define COMMTHRD_OPCODE_BCAST              0x05  /* !< Interrupt route - Dispatched on all cores */
#define COMMTHRD_OPCODE_ROUTEMASK          0x0F  /* !< Interrupt route mask */
#define COMMTHRD_OPCODE_CALLFUNC           0x10  /* !< The provided function will be called on the comm. thread */
#define COMMTHRD_OPCODE_DISABLEINTONENTRY  0x20  /* !< Interrupts using cntrid will be disabled when comm. thread is invoked */
#define COMMTHRD_OPCODE_ENABLEINTONPOOF    0x40  /* !< Interrupts using cntrid will be enabled when comm. thread poofs */


/*!
 * \brief Sets kernel data structures needed to dispatch a communications thread
 *
 * Each interrupt on BGP can be used to launch a communications thread.  Since access to the
 * interrupt controller is privileged, the function exposes some interrupt control to the
 * user application.
 * \pre Counter must have been allocated via the LockBox_AllocateCounter() routine.
 * \pre It is recommended that Kernel_DisableInteruptClass() be called twice on the counter
 *      to ensure that the interrupt is disabled until all interrupts for the counter
 *      have been configured.
 * \pre All
 * \post After the last call to Kernel_SetCommThreadConfig for the counter, invoke
 *       Kernel_EnableInterruptClass() and Kernel_HardEnableInterruptClass() on
 *       that counter to enable the interrupts for that class.
 * \see LockBox_AllocateCounter
 * \see Kernel_DisableInterruptClass
 * \see Kernel_EnableInterruptClass
 * \see Kernel_HardEnableInterruptClass
 * \note An interrupt can only belong to 1 interrupt class (a.k.a., lockbox counter)
 * \note The effects of this function span the entire node regardless of SMP, Dual, or VNM settings.
 * \note Kernel may prevent changing interrupt settings for certain InterruptID values.
 * \note If an interrupt fires on a core without a comm. thread, results are not guaranteed.
 * \return Completion status of the command.
 * \retval 0 no error occurred
 * \retval EINVAL invalid parameter
 * \param[in] InterruptID  Identifies a unique interrupt line.  \see Kernel_MkInterruptID
 * \param[in] opcode       Specifies what operation to perform when the interrupt occurs. Valid \ref COMMTHRD_OPCODES
 * \param[in] cntrid       ID of the lockbox counter used for interrupt enable/disable control
 * \param[in] funcptr      Function pointer that will be invoked when the interrupt fires
 * \param[in] arg1         1st argument to the funcptr when the interrupt fires
 * \param[in] arg2         2nd argument to the funcptr when the interrupt fires
 * \param[in] arg3         3rd argument to the funcptr when the interrupt fires
 *
 */
typedef uint32_t* LockBox_Counter_t;  /*!< Counter ID definition */
int Kernel_SetCommThreadConfig(int InterruptID,
			       int opcode,
			       LockBox_Counter_t cntrid,
			       Kernel_CommThreadHandler funcptr,
			       uint32_t arg1,
			       uint32_t arg2,
			       uint32_t arg3,
			       uint32_t arg4);



/*! \brief Indicates that the kernel should disable the interrupt
 *
 * Updates the interrupt class's lockbox to indicate that the kernel should disable the interrupt.
 * Kernel will disable the interrupt at its leisure, but it should ensure that no communications thread
 * is invoked for that interrupt class.
 *
 * The lockbox values have the following meanings:
 * 0: Interrupts for this classid are enabled
 * 1: Interrupts for this classid are logically disabled.
 *    If an interrupt occurs, the kernel will hard-disable them and ignore the interrupt.
 * 2: Interrupts for this classid are hard-disabled.  The interrupt will not disturb the core.
 *
 * \note The effects of this function span the entire node regardless of SMP, Dual, or VNM settings.
 * \note Do not disable an already disabled interrupt class.
 * \note A disabled interrupt class is disabled for all 4 cores, regardless of mode.
 * \param[in] classid An allocated lockbox that is being used to control a set of interrupt enable/disable lines
 *
 */
uint32_t Kernel_DisableInterruptClass(LockBox_Counter_t classid);



/*!
 * \brief Enables/Disables the counter overflow/underflow interrupts
 *
 * This function is a wrapper around a system call that can enable or disable the 4 counter overflow/underflow interrupts
 *
 * \param[in]  enable/disable boolean
 *
 * \retval  0            Successful
 * \retval  error_value  An error value defined in the _BGP_RAS_DMA_ErrCodes
 *                       enum located in bgp/arch/include/common/bgp_ras.h
 *
 */
int Kernel_ChgCounterInterruptEnables(uint32_t enable);


/* int rts_get_personality( void * pers, size_t size ); */


/*!
 * \brief Update mapping info about physically contigouos application memory regions
 *        ( used only in HPC mode )
 */
int Kernel_UpdateAppSegmentInfo(void);



/*!
 * \brief Internal helper function for virtual to physical address translation
 *
 */

int User_Virtual2Physical(unsigned long vaddr,      /*  32bit Virtual start address */
			  size_t   vsize,           /*  size in bytes of virtual range */
			  uint32_t *ua_out,         /*  upper 4 Physical Address bits */
			  uint32_t *pa_out );


/*! \brief Translate a 32bit Virtual Address to a 36bit Physical Address, returning separated upper and lower parts.
 *
 * \param[in] pVA   32bit virtual address in the calling process
 * \param[in] vsize size in bytes of the virtual range
 * \param[out] ua_out upper 4 physical address bits
 * \param[out] pa_out lower 32 physical address bits
 * \return Error condition for translation
 * \retval  0 Successful translation, with ua_out and pa_out filled in
 * \retval -1 Invalid Virtual Address for this process, ua_out and pa_out unmodified.
 * \retval -2 The range from vaddr to (vaddr+vsize) is not physically contiguous.
 * \retval -3 vaddr in Scratch, but no Scratch, or not enough Scratch, is enabled.
 * \retval -4 invalid parameter
 *
 *  \warning Supports only Text, Data, Stack, and (optional) eDRAM Scratch translation
 *  \warning CNK "pagesize" is 1MB.
 *  \warning Text and Data are virtually contiguous, but not necessarily physically contiguous.
 *  \todo Does not (currently) support > 4GB DDR space.
 *  \todo Does not (currently) support Shared Memory Area.
 */
int Kernel_Virtual2Physical( void     *pVA,      /*  input: 32bit Virtual start address */
			     size_t   vsize,     /*  input: size in bytes of virtual range */
			     uint32_t *ua_out,   /*  output: upper  4 Physical Address bits */
			     uint32_t *pa_out );  /*  output: lower 32 Physical Address bits */


/*!
 * \brief Query Free DMA Counter Subgroups within a Group
 *
 * This function is a wrapper around a system call that returns a list of the
 * free (available) subgroups within the specified group.
 *
 * \param[in]   type           Specifies whether this is an injection or
 *                             reception counter group (DMA_Type_Injection
 *                             or DMA_Type_Reception)
 * \param[in]   grp            Group number being queried (0 to
 *                             DMA_NUM_COUNTER_GROUPS-1)
 * \param[out]  num_subgroups  Pointer to an int where the number of free
 *                             subgroups in the specified group is returned
 * \param[out]  subgroups      Pointer to an array of num_subgroups ints where
 *                             the list of num_subgroups subgroups is returned.
 *                             Each int is the subgroup number
 *                             (0 to DMA_NUM_COUNTERS_PER_SUBGROUP-1).  The
 *                             caller must provide space for
 *                             DMA_NUM_COUNTERS_PER_SUBGROUP ints, in case the
 *                             entire counter group is free.
 *
 * \retval  0  Successful.  num_subgroups and subgroups array set as described.
 * \retval  -1 Unsuccessful.  errno gives the reason.
 *
 * \internal This function is not intended to be called directly
 * \see DMA_CounterGroupQueryFree()
 * \note The kernel may need to synchronize with other cores performing
 *       allocate or free syscalls.
 *
 */
uint32_t Kernel_CounterGroupQueryFree(uint32_t   type,
				      uint32_t   group,
				      uint32_t * num_subgroups,
				      uint32_t * subgroups);


/*!
 * \brief Allocate DMA Counters From A Group
 *
 * This function is a wrapper around a system call that allocates DMA counters
 * from the specified group.  Counters may be allocated in subgroups of
 * DMA_NUM_COUNTERS_PER_SUBGROUP counters.  Parameters specify how interrupts,
 * generated when a counter hits zero, are to be handled.  A
 * DMA_CounterGroup_t structure is returned for use in other inline
 * functions to operate on the allocated counters.
 *
 * \param[in]   type           Specifies whether this is an injection or
 *                             reception counter group (DMA_Type_Injection
 *                             or DMA_Type_Reception)
 * \param[in]   grp            Group number whose counters are being allocated
 *                             (0 to DMA_NUM_COUNTER_GROUPS-1)
 * \param[in]   num_subgroups  Number of subgroups to be allocated from the group
 *                             (1 to DMA_NUM_COUNTERS_PER_SUBGROUP)
 * \param[in]   subgroups      Pointer to an array of num_subgroups ints where
 *                             the list of subgroups to be allocated is provided.
 *                             Each int is the subgroup number
 *                             (0 to num_subgroups-1).
 * \param[in]   target         The core that will receive the interrupt when a
 *                             counter in this allocation hits zero
 *                             (0 to DMA_NUM_COUNTER_GROUPS-1)
 * \param[in]   handler        A pointer to the function to receive control in
 *                             the I/O thread to handle the interrupt when a
 *                             counter in this allocation hits zero.  This
 *                             function must be coded to take 3 parameters:
 *                               void*  A pointer to storage specific to this
 *                                      handler.  This is the handler_parm
 *                                      specified on this allocation function.
 *                               int    The counter's subgroup number (0 to
 *                                      DMA_NUM_COUNTER_SUBGROUPS-1).
 *                                      Note this number spans all groups.
 *                             If handler is NULL, hit-zero interrupts will not
 *                             be enabled for these counters.
 * \param[in]   handler_parm   A pointer to storage that should be passed to the
 *                             interrupt handling function (see handler
 *                             parameter)
 * \param[in]   interruptGroup A InterruptGroup_t that identifies the
 *                             group of interrupts that the counters being
 *                             allocated will become part of.
 * \param[out]  cg_ptr         Pointer to a structure that is filled in upon
 *                             successful return for use in other inline
 *                             functions to operate on the allocated counters.
 *                             \li counter -     Array of software counter
 *                                               structures.  Each element
 *                                               points to the corresponding
 *                                               hardware counter in DMA SRAM.
 *                                               Pointers are null if not
 *                                               allocated).
 *                                               Counters are initialized to
 *                                               DMA_COUNTER_INIT_VAL,
 *                                               disabled, their hit_zero bit
 *                                               is off, base and max are NULL.
 *                             \li status_ptr  - Points to status area within the
 *                                               DMA memory map.
 *                             \li permissions - Bits set for each allocated
 *                                               counter
 *                             \li grp_permissions - Permissions for each
 *                                                   subgroup
 *                             \li group_id    - The group number
 *                             \li type        - The type of DMA (injection or
 *                                               reception)
 *
 * \retval  0  Successful.  Counters allocated and cg_ptr structure filled in as
 *                          described.
 * \retval  -1 Unsuccessful.  errno gives the reason.  Nothing has been
 *                            allocated.
 *
 * \internal This function is not intended to be called directly
 * \see DMA_CounterGroupAllocate()
 * \note The kernel may need to synchronize with other cores performing queries
 *       or frees.
 *
 */
uint32_t Kernel_CounterGroupAllocate(uint32_t   type,
				     uint32_t   group,
				     uint32_t   num_subgroups,
				     uint32_t * subgroups,
				     uint32_t   target,
				     uint32_t   handler,
				     uint32_t * handler_parm,
				     uint32_t   interruptGroup,
				     uint32_t * cg_ptr);


/*!
 * \brief Free DMA Counters From A Group
 *
 * This function is a wrapper around a system call that frees DMA counters
 * from the specified group.  Counters may be freed in subgroups of
 * DMA_NUM_COUNTERS_PER_SUBGROUP counters.
 *
 * \param[in]   grp            Group number whose counters are being freed
 *                             (0 to DMA_NUM_COUNTER_GROUPS-1)
 * \param[in]   num_subgroups  Number of subgroups to be freed from the group
 *                             (1-DMA_NUM_COUNTERS_PER_SUBGROUP)
 * \param[in]   subgroups      Pointer to an array of num_subgroups ints where
 *                             the list of subgroups to be freed is provided.
 *                             Each int is the subgroup number
 *                             (0 to DMA_NUM_COUNTERS_PER_SUBGROUP-1).
 * \param[out]  cg_ptr         Pointer to the structure previously filled in when
 *                             these counters were allocated.  Upon successful
 *                             return, this structure is updated to reflect the
 *                             freed counters:
 *                             \li counter[]  -  Counter structures Pointers to
 *                                               freed counters nulled.
 *                             \li permissions - Bits cleared for each freed
 *                                               counter.
 *
 * \retval  0  Successful.  Counters freed and cg_ptr structure updated as
 *                          described.
 * \retval  -1 Unsuccessful.  errno gives the reason.
 *
 * \internal This function is not intended to be called directly
 * \see DMA_CounterGroupFree()
 * \note The kernel may need to synchronize with other cores performing allocates
 *       or queries.
 */
uint32_t Kernel_CounterGroupFree( uint32_t   group,
				  uint32_t   num_subgroups,
				  uint32_t * subgroups,
				  uint32_t * cg_ptr );


/*!
 * \brief Query Free DMA InjFifos within a Group
 *
 * This function is a wrapper around a system call that returns a list of the
 * free (available to be allocated) fifos within the specified group.
 *
 * \param[in]   grp            Group number being queried
 *                             (0 to DMA_NUM_INJ_FIFOS_PER_GROUP-1)
 * \param[out]  num_fifos      Pointer to an int where the number of free
 *                             fifos in the specified group is returned
 * \param[out]  fifo_ids       Pointer to an array of num_fifos ints where
 *                             the list of free fifos is returned.
 *                             Each int is the fifo number
 *                             (0 to DMA_NUM_INJ_FIFOS_PER_GROUP-1).
 *                             The caller must provide space for
 *                             DMA_NUM_INJ_FIFOS_PER_GROUP ints,
 *                             in case the entire fifo group is free.
 *
 * \retval  0  Successful.  num_fifos and fifo_ids array set as described.
 * \retval  -1 Unsuccessful.  errno gives the reason.
 * \internal This function is not intended to be called directly
 * \see DMA_InjFifoGroupQueryFree()
 */

uint32_t Kernel_InjFifoGroupQueryFree( uint32_t   group,
				       uint32_t * num_fifos,
				       uint32_t * fifo_ids);


/*!
 * \brief Allocate DMA InjFifos From A Group
 *
 * This function is a wrapper around a system call that allocates specified
 * DMA injection fifos from the specified group.  Parameters specify whether
 * each fifo is high or normal priority, local or non-local, and which torus
 * fifos it maps to.  A DMA_InjFifoGroup_t structure is returned for
 * use in other inline functions to operate on the allocated fifos.
 *
 * Refer to the interrupt discussion at the top of this include file to see why
 * there are no interrupt-related parameters.
 *
 * \param[in]   grp          Group number whose DMA injection fifos are being
 *                           allocated (0 to DMA_NUM_INJ_FIFO_GROUPS-1)
 * \param[in]   num_fifos    Number of fifos to be allocated from the group
 *                           (1 to DMA_NUM_INJ_FIFOS_PER_GROUP)
 * \param[in]   fifo_ids     Pointer to an array of num_fifos ints where
 *                           the list of fifos to be allocated is provided.
 *                           Each int is the fifo number (0 to num_fifos-1).
 * \param[in]   priorities   Pointer to an array of num_fifos short ints where
 *                           the list of priorities to be assigned to the fifos
 *                           is provided.  Each short int indicates the priority
 *                           to be assigned to each of the fifos identified in
 *                           the fifo_ids array (0 is normal, 1 is high priority).
 * \param[in]   locals       Pointer to an array of num_fifos short ints where
 *                           an indication is provided of whether each fifo will
 *                           be used for local transfers (within the same node)
 *                           or torus transfers.  Each short int indicates the
 *                           local/non-local attribute to be assigned to each of
 *                           the fifos identified in the fifo_ids array (0 is
 *                           non-local, 1 is local).  If 0, the corresponding
 *                           array element in ts_inj_maps indicates which torus
 *                           fifos can be injected.
 * \param[in]   ts_inj_maps  Pointer to an array of num_fifos short ints where
 *                           the torus fifos that can be injected are specified
 *                           for each fifo.  Each short int specifies which of
 *                           the 8 torus injection fifos can be injected when a
 *                           descriptor is injected into the DMA injection fifo.
 *                           Must be non-zero when the corresponding "locals"
 *                           is 0.
 * \param[out]  fg_ptr       Pointer to a structure that is filled in upon
 *                           successful return for use in other inline functions
 *                           to operate on the allocated fifos.
 *                           \li fifos - Array of fifo structures.  Structures
 *                                       for allocated fifos are initialized as
 *                                       documented below.  Structures for
 *                                       fifos not allocated by this instance of
 *                                       this syscall are initialized to binary
 *                                       zeros.  Allocated fifos are enabled.
 *                           \li status_ptr  - Points to status area within the
 *                                             DMA memory map.
 *                           \li permissions - Bits indicating which fifos were
 *                                             allocated during this syscall.
 *                           \li group_id    - The id of this group.
 *
 * \retval  0  Successful.  Fifos allocated and fg_ptr structure filled in as
 *                          described.
 * \retval  -1 Unsuccessful.  errno gives the reason.
 *
 * \internal This function is not intended to be called directly
 * \see DMA_InjFifoGroupAllocate()
 * \return The group fifo structure pointed to by fg_ptr is completely
 *         initialized as follows:
 *         - status_ptr points to the appropriate fifo group DMA memory map
 *         - fifo structures array.  Fifo structures for fifos not allocated
 *           during this syscall are initialized to binary zeros.  Fifo
 *           structures for fifos allocated during this syscall are initialized:
 *             - fifo_hw_ptr points to the DMA memory map for this fifo.  The
 *               hardware start, end, head, and tail are set to zero by the
 *               kernel.
 *             - All other fields in the structure are set to zero by the kernel
 *               except priority, local, and ts_inj_map are set to reflect what
 *               was requested in the priorities, locals, and ts_inj_maps
 *               syscall parameters.
 *
 */
uint32_t Kernel_InjFifoGroupAllocate( uint32_t   group,
				      uint32_t   num_fifos,
				      uint32_t * fifo_ids,
				      uint16_t * priorities,
				      uint16_t * locals,
				      uint8_t  * ts_inj_maps,
				      uint32_t * fg_ptr );



/*!
 * \brief Free DMA InjFifos From A Group
 *
 * This function is a wrapper around a system call that frees DMA injection
 * counters from the specified group.
 *
 * \param[in]   grp          Group number whose DMA injection fifos are being
 *                           freed (0 to DMA_NUM_INJ_FIFO_GROUPS-1)
 * \param[in]   num_fifos    Number of fifos to be freed from the group
 *                           (1 to DMA_NUM_INJ_FIFOS_PER_GROUP)
 * \param[in]   fifo_ids     Pointer to an array of num_fifos ints where
 *                           the list of fifos to be freed is provided.
 *                           Each int is the fifo number (0 to num_fifos-1).
 * \param[in]   fg_ptr       Pointer to the structure previously filled in when
 *                           these fifos were allocated.  Upon successful
 *                           return, this structure is updated to reflect the
 *                           freed fifos:
 *                           \li fifos - Structures for freed fifos zero'd.
 *                                       Freed fifos are disabled.
 *                           \li permissions - Bits cleared for each freed fifo.
 *
 * \retval  0  Successful.  Fifos freed and fg_ptr structure updated as described.
 * \retval  -1 Unsuccessful.  errno gives the reason.
 *
 * \internal This function is not intended to be called directly
 * \see DMA_InjFifoGroupFree()
 * \note  This is a fatal error if any of the fifos are non empty and activated
 *
 */
uint32_t Kernel_InjFifoGroupFree(uint32_t   group,
				 uint32_t   num_fifos,
				 uint32_t * fifo_ids,
				 uint32_t * fg_ptr);



/*!
 * \brief DMA InjFifo Initialization By Id
 *
 * - For an allocated injection DMA fifo, initialize its start, head, tail, and
 *   end.
 * - Compute fifo size and free space.
 * - Initialize wrap count.
 * - Activate the fifo.
 *
 * \param[in]  fg_ptr    Pointer to fifo group structure.
 * \param[in]  fifo_id   Id of the fifo to be initialized
 *                       (0 to DMA_NUM_INJ_FIFOS_PER_GROUP-1).
 * \param[in]  va_start  Virtual address of the start of the fifo.
 * \param[in]  va_head   Virtual address of the head of the fifo (typically
 *                       equal to va_start).
 * \param[in]  va_end    Virtual address of the end of the fifo.
 *
 * \retval   0  Successful.
 * \retval  -1  Unsuccessful.  Error checks include
 *              - va_start < va_end
 *              - va_start <= va_head <=
 *                  (va_end - DMA_FIFO_DESCRIPTOR_SIZE_IN_QUADS)
 *              - va_start and va_end are 32-byte aligned
 *              - fifo_size is larger than (DMA_MIN_INJECT_SIZE_IN_QUADS +
 *                                          DMA_FIFO_DESCRIPTOR_SIZE_IN_QUADS)
 *
 */
uint32_t Kernel_InjFifoInitById(uint32_t * fg_ptr,
				int        fifo_id,
				uint32_t * va_start,
				uint32_t * va_head,
				uint32_t * va_end);



/*!
 * \brief Set DMA Reception Fifo Map
 *
 * This function is a wrapper around a system call that
 * - Sets DCRs establishing the map between the hardware torus fifos and the
 *   DMA reception fifos that are to receive the packets from those hardware
 *   torus fifos.
 * - Sets DCRs establishing the DMA reception fifos that are to receive
 *   local transfer packets.
 * - Sets the DCRs establishing the type (0 or 1) of each reception fifo.
 * - Sets the DCRs establishing the threshold for type 0 and 1 reception fifos.
 * - Leaves all of the fifos that are used in a "disabled" state.
 *   DMA_RecFifoInitById() initializes and enables the fifos.
 *
 * \param[in]  rec_map  Reception Fifo Map structure, defining the mapping.
 *
 * \retval  0            Successful
 * \retval  error_value  An error value defined in the _BGP_RAS_DMA_ErrCodes
 *                       enum located in bgp/arch/include/common/bgp_ras.h
 *
 * \internal This is an internal syscall
 * \see DMA_RecFifoSetMap
 * \note  This function should be called once per job, after DMA_ResetRelease().
 *        It may be called by any core, but once a core has called it, other
 *        calls by that same core or any other core will fail.
 *
 * \note  During job init, the kernel sets up the DCR clear masks for each
 *        reception fifo group (DCRs 0xD68 - 0xD6C) such that a write to clear
 *        a fifo in group g only clears group g.
 *
 */
int Kernel_RecFifoSetMap(uint32_t* rec_map);


/*!
 * \brief Get DMA Reception Fifo Map
 *
 * This function is a wrapper around a system call that returns a DMA
 * reception fifo map structure, filled in according to the DCRs.
 *
 * \param[in,out]  rec_map  A pointer to a Reception Fifo Map structure
 *                          that will be filled-in upon return.
 *
 * \retval  0            Successful
 * \retval  error_value  An error value defined in the _BGP_RAS_DMA_ErrCodes
 *                       enum located in bgp/arch/include/common/bgp_ras.h
 *
 */
int Kernel_RecFifoGetMap(uint32_t* rec_map);



/*!
 * \brief Get DMA Reception Fifo Group
 *
 * This is a wrapper around a System Call. This function returns THE
 * one-and-only pointer to the fifo group structure, with the entries all
 * filled in from info in the DCRs.  If called multiple times with the same
 * group, it will always return the same pointer, and the system call will
 * not be invoked again.
 *
 * It must be called AFTER DMA_RecFifoSetMap().
 *
 * By convention, the same "target" is used for normal and header fifo
 * interrupts (could be changed).  In addition, by convention, interrupts for
 * fifos in group g come out of the DMA as non-fatal irq bit 28+g,
 * ie, only fifos in group g can cause the "type g" threshold interrupts.
 *
 * \param[in]  grp      The group number (0 through DMA_NUM_REC_FIFO_GROUPS).
 * \param[in]  target   The core that will receive the interrupt when a
 *                      fifo in this group reaches its threshold
 *                      (0 to DMA_NUM_REC_FIFO_GROUPS-1).
 *                      Ignored on subsequent call with the same group.
 * \param[in]  normal_handler  A pointer to the function to receive control in
 *                             the I/O thread to handle the interrupt when a
 *                             normal fifo in this group reaches its threshold.
 *                             This function must be coded to take 2 parameters:
 *                               void* A pointer to storage specific to this
 *                                     handler.  This is the normal_handler_parm
 *                                     specified on this function call.
 *                               int   The global fifo ID of the fifo that hit
 *                                     its threshold (0 through
 *                                     NUM_DMA_REC_FIFOS-1).
 *                             If normal_handler is NULL, threshold interrupts
 *                             are not delivered for normal fifos in this group.
 *                             Ignored on subsequent call with the same group.
 * \param[in]  normal_handler_parm   A pointer to storage that should be passed
 *                                   to the normal interrupt handling function
 *                                   (see normal_handler parameter).
 *                                   Ignored on subsequent call with the same
 *                                   group.
 * \param[in]  header_handler  A pointer to the function to receive control in
 *                             the I/O thread to handle the interrupt when a
 *                             header fifo in this group reaches its threshold.
 *                             This function must be coded to take 2 parameters:
 *                               void* A pointer to storage specific to this
 *                                     handler.  This is the header_handler_parm
 *                                     specified on this function call.
 *                               int   The global fifo ID of the fifo that hit
 *                                     its threshold (0 through
 *                                     NUM_DMA_REC_FIFOS-1).
 *                             If header_handler is NULL, threshold interrupts
 *                             are not delivered for header fifos in this group.
 *                             Ignored on subsequent call with the same group.
 * \param[in]  header_handler_parm   A pointer to storage that should be passed
 *                                   to the header interrupt handling function
 *                                   (see header_handler parameter).
 *                                   Ignored on subsequent call with the same
 *                                   group.
 * \param[in]  interruptGroup  A InterruptGroup_t that identifies the
 *                             group of interrupts that the fifos in this group
 *                             will become part of.
 *                             Ignored on subsequent call with the same group.
 *
 * \return  RecFifoGroupStruct  Pointer to a DMA Reception Fifo Group structure
 *                              that reflects the fifos that are being used in
 *                              this group.  This same structure is shared by
 *                              all users of this reception fifo group.
 *                              NULL is returned if an error occurs.
 *
 * \note  The following comments from Phil about the internals of the syscall:
 *   - error checks
 *     - 0 <= group_id < 4
 *     - the start of the fifo group is a valid virtual address (tlb mapped)?
 *   - disable the rDMA
 *   - call _BGP_rDMA_Fifo_Get_Map to get the DCR mapping information
 *   - loop through the map to determine how many and which fifos in this group
 *     are used, including headers
 *   - filling in the addresses of used fifos
 *     - In particular, any pointer to any fifo in the group that is not used
 *       will have a null pointer
 *   - furthermore,
 *     - write starting values to all used fifos
 *     - make sure all interrupts are cleared
 *     - enable rDMA
 *
 */
int Kernel_RecFifoGetFifoGroup(
			       uint32_t * fifogroup,
			       int        grp,
			       int        target,
			       void     * normal_handler,
			       void     * normal_handler_parm,
			       void     * header_handler,
			       void     * header_handler_parm,
			       void     * interruptGroup
			       );



/*!
 * \brief DMA RecFifo Initialization By Id
 *
 * - For a DMA reception fifo, initialize its start, head, tail, and end.
 * - Compute fifo size and free space.
 *
 * \param[in]  fg_ptr    Pointer to fifo group structure.
 * \param[in]  fifo_id   Id of the fifo to be initialized
 *                       (0 to DMA_NUM_REC_FIFOS_PER_GROUP-1).
 * \param[in]  va_start  Virtual address of the start of the fifo.
 * \param[in]  va_head   Virtual address of the head of the fifo (typically
 *                       equal to va_start).
 * \param[in]  va_end    Virtual address of the end of the fifo.
 *
 * \retval   0  Successful.
 * \retval  -1  Unsuccessful.  Error checks include
 *              - va_start <  va_end
 *              - va_start <= va_head < va_end
 *              - va_start and va_end are 32-byte aligned
 *              - fifo_size >= DMA_MIN_REC_FIFO_SIZE_IN_BYTES
 *
 */
int Kernel_RecFifoInitById( uint32_t * fg_ptr,
			    int        fifo_id,
			    void     * va_start,
			    void     * va_head,
			    void     * va_end );




#endif  /*  Add nothing below this line */
