#ifndef BOTANIK_CODEGEN_ABI_H
#define BOTANIK_CODEGEN_ABI_H

/**
 * @file codegen_abi.h
 * @brief The version of the contract between the generator's output and the
 *        library that compiles it.
 *
 * scripts/sitegen reads it and stamps it into rows.gen.h; vars/rows.h refuses a
 * generated table carrying another. Bump it with any change to a template or
 * to the generator that the library's headers depend on.
 */
#define BOTANIK_CODEGEN_ABI 1

#endif // BOTANIK_CODEGEN_ABI_H
