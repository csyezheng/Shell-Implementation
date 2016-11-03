//
// Created by csyezheng on 11/3/16.
//

#include <cstdlib>
#include <cstdio>
#include <cstring>                // strcpy
#include <iostream>
#include <map>
#include <sstream>
#include <vector>
#include <string>

/*                                // Potentially useful #includes (either here or in builtins.h):
#include <readline/readline.h>
#include <readline/history.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/errno.h>
#include <sys/param.h>
 */

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "builtins.h"

using namespace std;

// The characters will use to delimit words
const char *const WORD_DELIMITERS = " \t\n\"\\'`@><=;|&{(";

// The max size of commline can read
const int MAXLINE = 128;

// An external reference to the execution environment
extern char **environ;

extern vector<string> event_list;

extern map<string, string>  alias_list;

// Define 'command' as a type for built-in commands
using command =  int(*)(vector<string> &);

// A mapping of internal commands to their corresponding functions
map<string, command> builtins;

// Variables local to the shell
map<string, string> localvars;

int execute_line(vector<string> &tokens, map<string, command> &builtins);

// Count the background process ID.
static int backgroundProcess = 0;

// Converter for transform
char *convert(const string &s)
{
    char *result = new char[s.size() + 1];
    strcpy(result, s.c_str());
    return result;
}

void update_history(const string &command)
{
    event_list.push_back(command);
}

vector<string> parseline(const char *line)          // Parse the command line and build the argv sequence
{
    vector<string> tokens;
    string token;

    istringstream token_stream(line);

    while (token_stream >> token)
    {
        tokens.push_back(token);
    }

    // Search for quotation marks, which are explicitly disallowed
    for (size_t i = 0; i < tokens.size(); i++)
    {
        if (tokens[i].find_first_of("\"'`") != string::npos)
        {
            cerr << "\", ', and ` characters are not allowed." << endl;

            tokens.clear();
        }
    }

    return tokens;
}

// Examines each token and sets an env variable for any that are in the form of key=value.
void local_variable_assignment(vector<string> &tokens)
{
    vector<string>::iterator token = tokens.begin();

    // Return if the token is alias so it wouldn't remove the key=value
    if (*token == "alias") return;

    while (token != tokens.end())
    {
        string::size_type eq_pos = token->find("=");

        // If there is an equal sign in the token, assume the token is var=value
        if (eq_pos != string::npos)
        {
            string name = token->substr(0, eq_pos);
            string value = token->substr(eq_pos + 1);

            localvars[name] = value;

            token = tokens.erase(token);
        }
        else
        {
            ++token;
        }
    }
}

// Substitutes any tokens that start with a $ with their appropriate value, or
// with an empty string if no match is found.
void variable_substitution(vector<string> &tokens)
{
    vector<string>::iterator token;

    for (token = tokens.begin(); token != tokens.end(); ++token)
    {
        if (token->at(0) == '$')
        {
            string var_name = token->substr(1);

            if (getenv(var_name.c_str()) != NULL)
            {
                *token = getenv(var_name.c_str());
            }
            else if (localvars.find(var_name) != localvars.end())
            {
                *token = localvars.find(var_name)->second;
            }
            else
            {
                *token = "";
            }
        }
    }
}

// Check the command type.
vector<int> commandType(vector<string> tokens)
{
    vector<int> command(3);
    vector<string>::iterator iter;
    for (iter = tokens.begin(); iter != tokens.end(); iter++)
    {
        if (*iter == "|")
        {
            command[0] = 1;
        }
        else if (*iter == ">>" || *iter == ">")
        {
            command[1] = 1;
        }
        else if (*iter == "<")
        {
            command[2] = 1;
        }
    }
    return command;
}

void closePipes(int pipes[], int size, int except)
{
    for (int i = 0; i < size; i++)
    {
        if (i != except) close(pipes[i]);
    }
}

// Generate 2d vectors of the tokens for multiple commands.
vector<vector<string> > genMultiTokens(vector<string> tokens)
{
    vector<vector<string> > result;
    vector<string> tmp;
    vector<string>::iterator iter;

    for (iter = tokens.begin(); iter != tokens.end(); ++iter)
    {
        tmp.push_back(*iter);
        if (*iter == "|")
        {
            tmp.pop_back();
            result.push_back(tmp);
            tmp.clear();
        }
        else if (*iter == ">" || *iter == ">>" || *iter == "<")
        {
            tmp.pop_back();
            result.push_back(tmp);
            return result;
        }
    }
    result.push_back(tmp);
    tmp.clear();
    return result;
}

// Get direction tokens from a given line.
vector<string> extractDirection(vector<string> tokens)
{
    vector<string> result(2);
    vector<string>::iterator iter;
    int found = 0;
    for (int i = 0; i < tokens.size(); i++)
    {
        if (tokens[i] == ">>" || tokens[i] == ">" || tokens[i] == "<" ||
            found != 0)
        {
            result[found] = tokens[i];
            found++;
        }
    }
    return result;
}

// Handles piping. The input is 2d vector of commands. i.e : [ls],[cat],[grep,shell.cpp],[cut,-d,1-2]
int pipeAndFrd(vector<string> tokens)
{
    vector<vector<string> > multiTokens = genMultiTokens(tokens);
    vector<string> direction = extractDirection(tokens);
    // Need this for recording the status of each child.
    int status;
    int statusArr[multiTokens.size()];

    // Handle file redirection if there is a w and a.
    if (direction.size() >= 2)
    {
        if (direction[0] == ">")
        {
            FILE *createCat = freopen(direction[1].c_str(), "w", stdout);
        }
        else if (direction[0] == ">>")
        {
            FILE *createCat = freopen(direction[1].c_str(), "a", stdout);
        }
        else if (direction[0] == "<")
        {
            FILE *createCat = freopen(direction[1].c_str(), "r", stdin);
        }
    }

    // Initialize the pipes.
    // File descriptor size should be (n-1)*2 because there are 2 ends
    // in each pipe (write(1) and read(0)). i.e A <-> B <-> C
    // STD_IN : 0, STD_OUT : 1, STD_ERR : 2
    int pipeFdSize = (multiTokens.size() - 1) * 2;
    int pipes[pipeFdSize];

    // Wait for each child.
    pid_t waitresult;

    // Initilize the fork() array depending on the size of the commands.
    pid_t forkArr[multiTokens.size()];

    // 0 : success , -1 : error
    int return_value = -1;

    // Create the pipes. Notice here that we are offsetting by 2.
    // because a pipe has two ends which requires two slots in the array.
    for (int i = 0; i < pipeFdSize; i += 2)
    {
        pipe(pipes + i);
    }

    // Now the annoying part.
    for (int i = 0; i < multiTokens.size(); i++)
    {
        // Call fork() and check error.
        if ((forkArr[i] = fork()) == -1)
        {
            perror("Pipes fork error ");
            exit(-1);
        }

        // Child process should execute the commands. Before we do that
        // we have to put a pipe between the parent process and the
        // child process. We will use dup2 because it allows us to set
        // STD_OUT and STD_IN for a specified process.
        if (forkArr[i] == 0)
        {
            // We need to take care of the special cases : first pipe
            // and last pipe. The pipes in the middle have same behavior.
            // i.e : A<->B<->C<->D<->E
            if (i == 0)
            {
                // First pipe. Duplicate a file descriptor to STD_OUT.
                // A <-> B and A's out is set to the pipe's write.
                dup2(pipes[1], 1);
            }
            else if (i == multiTokens.size() - 1)
            {
                // Last index. A<->B...<->D and the last's command needs to read
                // from the pipe. Thus, we set the last pipe's read to STD_IN
                // from previous command.
                dup2(pipes[2 * (multiTokens.size() - 1) - 2], 0);
            }
            else
            {
                // Middle pipes. A...<->C<->...E We want to read the STD_OUT from B
                // to the pipe before C and write STD_IN to the pipe for the next
                // command.
                dup2(pipes[2 * (i - 1)], 0);
                dup2(pipes[(2 * i) + 1], 1);
            }
            // Always make sure to close the pipes. So then, the parent knows if
            // the child has reached to the end of file.
            closePipes(pipes, pipeFdSize, -1);
            // Execute the command
            return_value = execute_line(multiTokens[i], builtins);
            exit(return_value);
        }
    }

    // This is the parent process.
    closePipes(pipes, pipeFdSize, -1);

    // Check the status of each child and wait for finishing the execution.
    for (int i = 0; i < multiTokens.size(); i++)
    {
        waitresult = waitpid(forkArr[i], &status, WUNTRACED);
        statusArr[i] = status;
    }

    // Last status should indicate the success of the piping.
    if (statusArr[multiTokens.size() - 1] == 0)
    {
        return_value = 0;
    }

    // Close the files that are opened
    fclose(stdout);
    fclose(stdin);

    exit(return_value);
    return return_value;
}


// Handles external commands, redirects, and pipes.
int execute_external_command(vector<string> tokens)
{
    // We will use execvp(const char *path, const char *arg)
    // First generate the char args including the first command.
    pid_t subprocess;
    pid_t childresult;

    int status;
    int exitCode = -1;

    if ((subprocess = fork()) == -1)
    {
        perror("fork failed");
        return exitCode;
    }

    if (subprocess == 0)                                     // child process.
    {
        vector<int> commandVar = commandType(tokens);
        if (commandVar[0] || commandVar[1] || commandVar[2])
        {
            if (!(commandVar[0] && commandVar[2]) && !(commandVar[1] && commandVar[2]))
            {
                exitCode = pipeAndFrd(tokens);                  // for pipe and redirect
            }
            else
            {
                cout << "< cannot be combined with other file redirection. " << endl;
                return exitCode;
            }

        }
        else
        {
            char **arg = (char **) malloc(sizeof(char *) * tokens.size() + 2);
            for (int i = 0; i < tokens.size(); i++)
            {
                arg[i] = convert(tokens[i]);                 // Generate the arguments for execvp.
            }
            arg[tokens.size()] = nullptr;
            exitCode = execvp(tokens[0].c_str(), arg);      // Execute the command
            if (exitCode != 0)
            {
                perror("Execvp error ");
                _exit(-1);
            }
        }
    }
    else            // Parent process.
    {
        childresult = waitpid(subprocess, &status, WUNTRACED);
        if (status == 0)        // Success
        {
            exitCode = 0;
        }
    }
    return exitCode;
}

// Run a command in the background.
int runBackground(vector<string> &tokens)
{
    int return_value = 0;
    pid_t subprocess;

    if ((subprocess = fork()) == -1)
    {
        perror("Running in background error ");
        exit(1);
    }
    if (subprocess == 0)
    {
        // set pgid.
        setpgid(0, 0);
        // Remove the &.
        tokens.pop_back();
        // Execute the command.
        return_value = execute_line(tokens, builtins);
    }
    else
    {
        cout << "[" << backgroundProcess++ << "] " << subprocess << endl;
    }
    return return_value;
}


// Executes a line of input by either calling execute_external_command or
// directly invoking the built-in command.
int execute_line(vector<string> &tokens, map<string, command> &builtins)
{
    int return_value = 0;
    // cout << ">>>>>>>>>>>>>executing>>>>>>>>>>>>>>>" << endl;
    // for (int i = 0; i < tokens.size(); i++) {
    //  cout << tokens[i] << "-";
    //}
    // cout << endl;
    // First check if it's background command.
    if (tokens[tokens.size() - 1] == "&")
    {
        return_value = runBackground(tokens);
        return return_value;
    }

    // First check if it's piping. Then execute.
    vector<int> commandVar = commandType(tokens);
    // cout << "command size " << tokens.size() << endl
    //     << "command 0 : " << commandVar[0] << " command 1 : " << commandVar[1]
    //    << " command 2 : " << commandVar[2] << endl;
    if (commandVar[0] || commandVar[1] || commandVar[2])
    {
        return_value = execute_external_command(tokens);
    }
    else if (tokens.size() != 0)
    {
        map<string, command>::iterator cmd = builtins.find(tokens[0]);

        if (cmd == builtins.end())
        {
            return_value = execute_external_command(tokens);
        }
        else
        {
            return_value = ((*cmd->second)(tokens));
        }
    }

    return return_value;
}


/* eval - Evaluate a command line */
int eval(char *line)
{
    event_list.push_back(line);               // update history

    vector<string> tokens = parseline(line);

    // replace the tokens with the commands if the command is in the alias.
    auto aliasIter = alias_list.find(tokens[0]);
    if (aliasIter != alias_list.end())
    {
        tokens[0] = aliasIter->second;
    }

    local_variable_assignment(tokens);        // Handle local variable declarations
    variable_substitution(tokens);           // Substitute variable references

    // Execute the line
    return execute_line(tokens, builtins);
}

string get_prompt(int return_value)                       // Return a string representing the prompt to display to the user.
{
    string emocon = return_value ? ":(" : ":)";           // indicate the result (success or failure) of the last command.
    return pwd() + " prompt " + emocon + " $ ";
}



int main()
{
    // Populate the map of available built-in functions
    builtins["ls"] = &com_ls;
    builtins["cd"] = &com_cd;
    builtins["pwd"] = &com_pwd;
    builtins["alias"] = &com_alias;
    builtins["unalias"] = &com_unalias;
    builtins["echo"] = &com_echo;
    builtins["exit"] = &com_exit;
    builtins["history"] = &com_history;

    // The return value of the last command executed
    int return_value = 0;

    while (true)
    {
        string prompt = get_prompt(
                return_value);       // Get the prompt to show, based on the return value of the last command

        char line[MAXLINE];                            // the user input
        fgets(line, MAXLINE, stdin);

        if (!line)                                     // EOF
        {
            break;
        }

        return_value = eval(line);
    }

    return 0;
}
