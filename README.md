# Simple-shell-code
University project, some code was provided by the teacher.
These are some observations from the teacher:
-2*N pipe() calls instead of N-1 (for N commands);
-waitpid should be called for all children after all the forks are executed (to avoid the infinite buffer problem)

