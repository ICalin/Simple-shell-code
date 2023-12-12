/**
  * Shell framework
  * course Operating Systems
  * Radboud University
  * v22.09.05

  Student names:
  - Calin Iaru
  - Bernat Montilla
*/

/**
 * Hint: in most IDEs (Visual Studio Code, Qt Creator, neovim) you can:
 * - Control-click on a function name to go to the definition
 * - Ctrl-space to auto complete functions and variables
 */

// function/class definitions you are going to use
#include <iostream>
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <sys/param.h>
#include <signal.h>
#include <string.h>
#include <assert.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <vector>
#include <list>
#include <optional>

// although it is good habit, you don't have to type 'std' before many objects by including this line
using namespace std;

struct Command
{
  vector<string> parts = {};
};

struct Expression
{
  vector<Command> commands;
  string inputFromFile;
  string outputToFile;
  bool background = false;
};

// Parses a string to form a vector of arguments. The separator is a space char (' ').
vector<string> split_string(const string &str, char delimiter = ' ')
{
  vector<string> retval;
  for (size_t pos = 0; pos < str.length();)
  {
    // look for the next space
    size_t found = str.find(delimiter, pos);
    // if no space was found, this is the last word
    if (found == string::npos)
    {
      retval.push_back(str.substr(pos));
      break;
    }
    // filter out consequetive spaces
    if (found != pos)
      retval.push_back(str.substr(pos, found - pos));
    pos = found + 1;
  }
  return retval;
}

// wrapper around the C execvp so it can be called with C++ strings (easier to work with)
// always start with the command itself
// DO NOT CHANGE THIS FUNCTION UNDER ANY CIRCUMSTANCE
int execvp(const vector<string> &args)
{
  // build argument list
  const char **c_args = new const char *[args.size() + 1];
  for (size_t i = 0; i < args.size(); ++i)
  {
    c_args[i] = args[i].c_str();
  }
  c_args[args.size()] = nullptr;
  // replace current process with new process as specified
  int rc = ::execvp(c_args[0], const_cast<char **>(c_args));
  // if we got this far, there must be an error
  int error = errno;
  // in case of failure, clean up memory (this won't overwrite errno normally, but let's be sure)
  delete[] c_args;
  errno = error;
  return rc;
}

// Executes a command with arguments. In case of failure, returns error code.
int execute_command(const Command &cmd)
{
  auto &parts = cmd.parts;
  if (parts.size() == 0)
    return EINVAL;

  // execute external commands
  int retval = execvp(parts);
  return retval;
}

void display_prompt()
{
  char buffer[512];
  char *dir = getcwd(buffer, sizeof(buffer));
  if (dir)
  {
    cout << "\e[32m" << dir << "\e[39m"; // the strings starting with '\e' are escape codes, that the terminal application interpets in this case as "set color to green"/"set color to default"
  }
  cout << "$ ";
  flush(cout);
}

string request_command_line(bool showPrompt)
{
  if (showPrompt)
  {
    display_prompt();
  }
  string retval;
  getline(cin, retval);
  return retval;
}

// note: For such a simple shell, there is little need for a full-blown parser (as in an LL or LR capable parser).
// Here, the user input can be parsed using the following approach.
// First, divide the input into the distinct commands (as they can be chained, separated by `|`).
// Next, these commands are parsed separately. The first command is checked for the `<` operator, and the last command for the `>` operator.
Expression parse_command_line(string commandLine)
{
  Expression expression;
  vector<string> commands = split_string(commandLine, '|');
  for (size_t i = 0; i < commands.size(); ++i)
  {
    string &line = commands[i];
    vector<string> args = split_string(line, ' ');
    if (i == commands.size() - 1 && args.size() > 1 && args[args.size() - 1] == "&")
    {
      expression.background = true;
      args.resize(args.size() - 1);
    }
    if (i == commands.size() - 1 && args.size() > 2 && args[args.size() - 2] == ">")
    {
      expression.outputToFile = args[args.size() - 1];
      args.resize(args.size() - 2);
    }
    if (i == 0 && args.size() > 2 && args[args.size() - 2] == "<")
    {
      expression.inputFromFile = args[args.size() - 1];
      args.resize(args.size() - 2);
    }
    expression.commands.push_back({args});
  }
  return expression;
}

int execute_expression(Expression &expression)
{
  int comSize = expression.commands.size();

  // Check for empty expression
  if (comSize == 0)
    return EINVAL;

  // ********************** Internal commands (built-in) ***********************************************************
  // EXIT.
  // When exit is first keyword
  if (expression.commands[0].parts[0] == "exit")
    exit(0);

  // CD.
  // Check for invalid expression CD
  if (comSize > 1)
  {
    for (int i = 0; i < comSize; i++)
      if (expression.commands[i].parts[0] == "cd")
      {
        cerr << "Invalid expression: Incorrect use of cd. \nUse as a single command" << endl;
        return 0;
      }
  }
  // When cd is the first and only command
  if (expression.commands[0].parts[0] == "cd")
  {
    // If no path is specified, go home directory
    if (expression.commands[0].parts.size() == 1)
    {
      chdir(getenv("HOME"));
      return 0;
    }
    else
    {
      // If directory does not exist print error
      if (chdir(expression.commands[0].parts[1].c_str()) == -1)
        cerr << expression.commands[0].parts[1].c_str() << ":no such directory." << endl;
      return 0;
    }
  }

  // ******************* External commands ***********************************************************************
  // Loop over all commandos, and connect the output and input of the forked processes

  /*
  prev-pipe is a constant that keeps track of the read end of the previous command t
  in the child process
  loop over all the commands
  check if the first command contains an infile if yes we save it in prev_pipe, otherwise
  prev_pive is just STDIN_FILE
  STDIN of the current command is overwritten with the read end of the previous one,
  and the STDOUT  is the write end of the current one.
  in the parent close the STDOUT, and save the read end of the current pipe to use for the next command
  if we are at the last element check if we have and out file, if we have redirect it to STDOUT_FILE
  */

  int prev_pipe, p[2];
  pid_t child;

  for (int i = 0; i < comSize; i++)
  {
    pipe(p);

    // Check if pipe fails
    if (pipe(p) < 0)
      perror("Couldn't open pipe");

    child = fork();
    // Check if fork fails
    if (child < 0)
      perror("Fork failed");

    // Child process code
    if (child == 0)
    {
      /**************************************/
      // Dealing with STDIN
      // Check if the first process has a infile
      if (i == 0)
      {
        if (!expression.inputFromFile.empty())
        {
          prev_pipe = open(expression.inputFromFile.c_str(), O_RDONLY); // read-only
          if (prev_pipe == -1)
            perror("Couldn't open the input file\n");
          else
            dup2(prev_pipe, STDIN_FILENO);
        }
        else
          prev_pipe = STDIN_FILENO;
      }
      else
      {
        // Get STDIN from the previous pipe
        dup2(prev_pipe, STDIN_FILENO);
        close(prev_pipe);
      }

      /**************************************/
      // Dealing with STDOUT
      // Redirect stdout to current pipe
      if (i < comSize - 1)
      {
        dup2(p[1], STDOUT_FILENO);
        close(p[1]);
      }
      // If we are at the last element
      if (i == comSize - 1)
      {
        if (!expression.outputToFile.empty())
        {
          int outputStream = open(expression.outputToFile.c_str(), O_WRONLY | O_CREAT | O_TRUNC); // write-only, create if it does not exist, erase all content before writing tags
          if (outputStream == -1)
            perror("Couldn't open the output file\n");
          else
            dup2(outputStream, STDOUT_FILENO);
        }
      }

      execute_command(expression.commands[i]);
      cerr << "Command not found" << endl;
      abort();
    }

    // Parent process execution code
    close(p[1]);
    // Save read end of the current pipe to use in next iteration
    prev_pipe = p[0];
  }

  // Background running process
  if (!expression.background)
    waitpid(child, nullptr, 0);

  return 0;
}

int shell(bool showPrompt)
{
  while (cin.good())
  {
    string commandLine = request_command_line(showPrompt);
    Expression expression = parse_command_line(commandLine);
    int rc = execute_expression(expression);
    if (rc != 0)
      cerr << strerror(rc) << endl;
  }
  return 0;
}
