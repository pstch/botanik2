#ifndef BOTANIK_CODEGEN_ABI_H
#define BOTANIK_CODEGEN_ABI_H

/**
 * @file codegen_abi.h
 * @brief The version of the contract between the generator's output and the
 *        library that compiles it.
 *
 * GENERATED from scripts/sitegen/model.py's CODEGEN_ABI, which is where to
 * bump it. The generator stamps the same number into every site's rows.gen.h;
 * vars/rows.h refuses a generated table carrying another. Bump it with any
 * change to a template or to the generator that the library's headers depend
 * on - every stamp moves with it, because the generator's sources are hashed
 * into all of them.
 */
#define BOTANIK_CODEGEN_ABI 1

#endif // BOTANIK_CODEGEN_ABI_H
