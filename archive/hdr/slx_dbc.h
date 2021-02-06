/**
 *
 *  @file  slx_dbc.h
 *  @brief Defines assert() macros to support design by contract.
 *
 *  Assertions can be disabled by defining NDEBUG.
 *  DbC macros can be disabled by defining NASSERT.
 *
 *  DbC macros mimic Eiffel:
 *      REQUIRE   - test precondition
 *      ENSURE    - test postcondition
 *      INVARIANT - test invariant condition
 *      ALLEGE    - always perform test regardless of NASSERT or NDEBUG
 *
 *  Macros should not have side effects since they may be disabled. The
 *  exception is ALLEGE which will always execute but will not assert if
 *  NASSERT or NDEBUG is defined.
 *
 *  DbC macros from http://www.barrgroup.com/Embedded-Systems/How-To/Design-by-Contract-for-Embedded-Software
 *
 *  COPYRIGHT NOTICE: (c) 2016 DDPA LLC
 *  All Rights Reserved
 *
 */

#ifndef _slx_dbc_H_
#define _slx_dbc_H_


/// Fault_Handler is the vector target for unimplemented exceptions.
extern void Fault_Handler(void) __attribute__((naked));


#undef assert
#ifndef NDEBUG
#define assert(e) ((e) ? (void)0 : (void)(Fault_Handler(),0))
#else
#define assert(ignore) ((void)0)
#endif


/*
 *  NASSERT macro disables all contract validations
 *  (assertions, preconditions, postconditions, and invariants).
 */

#ifndef NASSERT           /* NASSERT not defined -- DbC enabled */
#define REQUIRE(test)     assert(test)
#define ENSURE(test)      assert(test)
#define INVARIANT(test)   assert(test)
#define ALLEGE(test)      assert(test)


#else                     /* NASSERT defined -- DbC disabled */
#define REQUIRE(ignore)   ((void)0)
#define ENSURE(ignore)    ((void)0)
#define INVARIANT(ignore) ((void)0)
#define ALLEGE(test)      ((void)(test))
#endif                    /* NASSERT */




#endif  /* _slx_dbc_H_ */
