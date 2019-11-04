/*
 * bfoc: brainfuck optimizing compiler
 * codeandkey
 *
 * BFOC will compile on any POSIX-compliant system and requires a valid gcc
 * in the PATH to compile code.
 *
 * The brainfuck optimizing compiler outputs C code which is then passed to
 * gcc. While unbounded tapes are nice, they are not a part of the official
 * brainfuck specification and has not been included in the runtime.
 *
 * This allows for smaller executable output as well.
 */

#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define GCC_EXECUTABLE      "gcc"
#define CODEGEN_TAPE_LENGTH 30000 /* https://en.wikipedia.org/wiki/Brainfuck#Language_design */
#define INITIAL_INPUT_BUF   256

/**
 * Locates the gcc executable path by searching the PATH environment.
 * If the command cannot be found or an error occurs, returns NULL.
 *
 * @return Allocated path to gcc if successful, NULL otherwise
 */
static char* locate_gcc();

/**
 * Generates a C function body from a brainfuck input source.
 * Writes the generated C source code to <out>.
 *
 * @param input_buf Brainfuck source buffer
 * @param input_len Brainfuck source length
 * @param out       File to write generated code to
 *
 * @return 0 if generation was successful, -1 if an error occurred
 */
static int generate_c_source(char* input_buf, int input_len, FILE* out);

/**
 * Outputs program usage to stderr.
 *
 * @param cmd First command line argument passed to main (argv[0])
 * @return    EXIT_FAILURE
 */
static int usage(char* cmd);

/**
 * Compiler entry point.
 *
 * @param argc Argument count
 * @param argv Argument list
 * @return Exit status
 */
int main(int argc, char** argv) {
    char* gcc_path = locate_gcc();

    if (gcc_path) {
        fprintf(stderr, "info: using gcc %s\n", gcc_path);
    } else {
        fprintf(stderr, "error: failed to locate gcc. Cannot proceed.\n");
        return 1;
    }

    /* Parse command-line options. */
    FILE* input_file = stdin;
    const char* output_file_path = "./a.out";

    int opt;
    while ((opt = getopt(argc, argv, "ho:")) != -1) {
        switch (opt) {
        default:
        case 'h':
            free(gcc_path);
            return usage(*argv);
        case 'o':
            output_file_path = optarg;
            break;
        }
    }

    if (optind < argc) {
        input_file = fopen(argv[optind], "r");

        if (!input_file) {
            free(gcc_path);
            fprintf(stderr, "error: failed to open input file %s for reading: %s\n", argv[optind], strerror(errno));
            return -1;
        }
    }

    /* Read all input source. */
    char c = 0;

    char* input_buf = malloc(INITIAL_INPUT_BUF);
    int input_buf_size = INITIAL_INPUT_BUF;
    int input_len = 0;

    while ((c = fgetc(input_file)) != EOF) {
        /* Ignore invalid characters early to keep the buffer clean. */
        switch (c) {
        case '+':
        case '-':
        case '>':
        case '<':
        case '[':
        case ']':
        case '.':
        case ',':
            break;
        default:
            continue;
        }

        input_buf[input_len++] = c;

        if (input_len >= input_buf_size) {
            input_buf_size *= 2;
            input_buf = realloc(input_buf, input_buf_size);
        }
    }

    input_buf[input_len] = '\0';
    fprintf(stderr, "info: read %d bytes of input code: %s\n", input_len, input_buf);

    /* Close input file if it's a real file. */
    if (input_file != stdin) {
        fclose(input_file);
    }

    /* Create the C source output file and open it */
    char c_output_filename[] = "/tmp/bfoc.XXXXXX.c";
    int c_output_file_fd = mkstemps(c_output_filename, 2);

    if (c_output_file_fd < 0) {
        fprintf(stderr, "error: Couldn't create temporary source file: %s", strerror(errno));
        return -1;
    }

    FILE* c_output_file = fdopen(c_output_file_fd, "w");

    if (!c_output_file) {
        fprintf(stderr, "error: Couldn't open temporary source file: %s", strerror(errno));
        free(gcc_path);
        return -1;
    }

    /* Write boilerplate code */
    time_t cur_time;
    time(&cur_time);

    fprintf(c_output_file, "/*\n * BFOC intermediate code\n * generated on %s */\n\n", ctime(&cur_time));
    fprintf(c_output_file, "#include <stdlib.h>\n#include <stdio.h>\n#include <stdint.h>\n\n");
    fprintf(c_output_file, "static uint8_t tape[%d];\nstatic int ptr;\n\n", CODEGEN_TAPE_LENGTH);
    fprintf(c_output_file, "int main() {\n");

    /* Write generated code to output. */
    if (generate_c_source(input_buf, input_len, c_output_file)) {
        fprintf(stderr, "error: error occurred during code generation. stopping..\n");
        fclose(c_output_file);
        free(gcc_path);
        return -1;
    }

    /* Write terminator boilerplate and close the output. */
    fprintf(c_output_file, "\treturn 0;\n}\n\n");
    fclose(c_output_file);

    fprintf(stderr, "info: wrote intermediate C source to %s\n", c_output_filename);

    /* Run gcc and generate the final output. */
    if (!fork()) {
        fprintf(stderr, "info: child process: starting gcc compile\n");
        int exec_status = execl(gcc_path, gcc_path, c_output_filename, "-o", output_file_path, NULL);

        if (exec_status) {
            fprintf(stderr, "error: child process: couldn't execute compiler: %s\n", strerror(errno));
        }

        return exec_status;
    } else {
        int child_status;
        wait(&child_status);

        if (child_status) {
            fprintf(stderr, "error: child process reported compile failed (code %d).\n", child_status);
        } else {
            fprintf(stderr, "info: successfully compiled output %s\n", output_file_path);
        }

        return child_status;
    }

    return 0;
}

char* locate_gcc() {
    /* Grab the PATH environment var. */
    char* path_var = getenv("PATH");

    if (!path_var) {
        fprintf(stderr, "error: the PATH variable is unset. Will not be able to find gcc.\n");
        return NULL;
    }

    int num_scanned = 0;

    /* Split the variable into directories. */
    for (char* cur_path = strtok(path_var, ":"); cur_path; cur_path = strtok(NULL, ":")) {
        /* Try to open the directory and locate gcc. */
        DIR* d = opendir(cur_path);

        if (!d) {
            continue; /* Invalid directory, skip to the next one */
        }

        num_scanned++;

        /* Iterate through directory entries. */
        struct dirent* dp;
        while ((dp = readdir(d))) {
            if (!strcmp(dp->d_name, GCC_EXECUTABLE)) {
                /* Located! Free up everything and return the final path. */
                int cur_path_len = strlen(cur_path);
                int gcc_exec_len = strlen(GCC_EXECUTABLE);

                char* gcc_path = malloc(2 + gcc_exec_len + cur_path_len);

                strncpy(gcc_path, cur_path, cur_path_len);
                gcc_path[cur_path_len] = '/';
                strncpy(gcc_path + cur_path_len + 1, GCC_EXECUTABLE, gcc_exec_len);
                gcc_path[cur_path_len + gcc_exec_len + 1] = '\0';

                closedir(d);

                return gcc_path;
            }
        }

        closedir(d);
    }

    /* Return failure if nothing was found. */
    fprintf(stderr, "error: Couldn't find gcc in PATH. (scanned %d dirs)\n", num_scanned);
    return NULL;
}

int generate_c_source(char* input_buf, int input_len, FILE* output_file) {
    int instr_count;
    int label_count = 0;
    int label_locations[26] = {-1};
    int label_stack = 0;
    int label_placed = 0;

    /* Don't increment i in the loop condition, as most of the cases
     * will increment it during scanning logic anyway. */
    for (int i = 0; i < input_len;) {
        fprintf(stderr, "debug: starting codegen for %c op\n", input_buf[i]);
        switch (input_buf[i]) {
        case '+':
            /* Walk through any consecutive '+' operators and bundle them into
             * one instruction. */
            instr_count = 1;
            while (input_buf[++i] == '+') ++instr_count;
            fprintf(output_file, "\ttape[ptr] += %d;\n", instr_count);
            break;
        case '-':
            /* Walk through any consecutive '-' operators and bundle them into
             * one instruction. */
            instr_count = 1;
            while (input_buf[++i] == '-') ++instr_count;
            fprintf(output_file, "\ttape[ptr] -= %d;\n", instr_count);
            break;
        case '>':
            /* Walk through any consecutive '>' operators and bundle them into
             * one instruction. */
            instr_count = 1;
            while (input_buf[++i] == '>') ++instr_count;
            fprintf(output_file, "\tptr += %d;\n", instr_count);
            break;
        case '<':
            /* Walk through any consecutive '<' operators and bundle them into
             * one instruction. */
            instr_count = 1;
            while (input_buf[++i] == '<') ++instr_count;
            fprintf(output_file, "\tptr -= %d;\n", instr_count);
            break;
        case '.':
            /* Output tape value */
            fprintf(output_file, "\tputchar(tape[ptr]);\n");
            ++i;
            break;
        case ',':
            /* Input tape value */
            fprintf(output_file, "\ttape[ptr] = getchar();\n");
            ++i;
            break;
        case '[':
            /* New loop point. For now, allow 26 labels, one for each letter. */
            if (label_count > 26) {
                fprintf(stderr, "error: source contains too many loops. The current maximum is 26 points.\n");
                return -1;
            }

            fprintf(output_file, "\tloop%c:\n", 'A' + label_count);
            label_locations[label_count++] = i;
            ++i;
            break;
        case ']':
            /* Walk back through the source code to find the matching label location. */
            label_placed = 0;
            for (int j = i; j >= 0; --j) {
                if (input_buf[j] == ']') label_stack++;
                if (input_buf[j] == '[') {
                    if (--label_stack <= 0) {
                        /* Found the matching label location. Search for the label ID. */
                        for (int k = 0; k < label_count; ++k) {
                            if (label_locations[k] == j) {
                                fprintf(output_file, "\tif (tape[ptr]) goto loop%c;\n", 'A' + k);
                                label_placed = 1;
                            }
                        }
                    }
                }
            }

            if (!label_placed) {
                fprintf(stderr, "error: failed to match loop end at location %d\n", i);
                return -1;
            }

            ++i;
            break;
        }
    }

    return 0;
}

int usage(char* cmd) {
    fprintf(stderr, "usage: %s [-h] [-o <output>] <input>\n", cmd);
    return EXIT_FAILURE;
}
