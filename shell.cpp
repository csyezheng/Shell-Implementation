#include <cstdlib>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>

// can't use fgets(), beacasue of the function can't let you backspace if you input error command
#include <readline/readline.h>
#include <readline/history.h>

#include "builtins.h"

// Potentially useful #includes (either here or in builtins.h):
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/errno.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace std;

// The characters that readline will use to delimit words
const char *const WORD_DELIMITERS = " \t\n\"\\'`@><=;|&{(";

// An external reference to the execution environment
extern char **environ;

extern vector<string> event_list;

extern map<string, vector<string> > alias_list;

// Define 'command' as a type for built-in commands
typedef int (*command)(vector<string> &);

// A mapping of internal commands to their corresponding functions
map<string, command> builtins;

// Variables local to the shell
map<string, string> localvars;

int execute_line(vector<string> &tokens, map<string, command> &builtins);

// Count the background process ID.
static int backgroundProcess = 0;

// Handles external commands, redirects, and pipes.
int execute_external_command(vector<string> tokens)
{
    pid_t subprocess;
    pid_t childresult;

    int status;
    int exitCode = -1;

    if ((subprocess = fork()) == -1)
    {
        perror("fork failed");
        return exitCode;
    }

    if (subprocess == 0)           // child process.
    {   
        vector<int> commandVar = commandType(tokens);
        if (commandVar[0] || commandVar[1] || commandVar[2])
        {
            if (!(commandVar[0] && commandVar[2]) &&
                !(commandVar[1] && commandVar[2]))
            {
                exitCode = pipeAndFrd(tokens);
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
                arg[i] = convert(tokens[i]);    // generate the char args including the first command.
            }

            arg[tokens.size()] = nullptr;

            // Execute the command
            exitCode = execvp(tokens[0].c_str(), arg);
            if (exitCode != 0)
            {
                perror("Execvp error ");
                _exit(-1);
            }
        }
    }
    else                                                // Parent process. Get that status of the child.
    {
        childresult = waitpid(subprocess, &status, WUNTRACED);

        if (status == 0)                                // Success
        {
            exitCode = 0;
        }
    }
    return exitCode;
}

// Converter for transform
char *convert(const string &s)
{
    char *result = new char[s.size() + 1];
    strcpy(result, s.c_str());
    return result;
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

void closePipes(int pipes[], int size, int except)
{
    for (int i = 0; i < size; i++)
    {
        if (i != except) close(pipes[i]);
    }
}

// Handles piping. The input is 2d vector of commands. i.e : [ls],[cat],[grep,shell.cpp],[cut,-d,1-2]
int pipeAndFrd(vector<string> tokens)
{
    vector<vector<string> > multiTokens = genMultiTokens(tokens);
    vector<string> direction = extractDirection(tokens);

    int status;                          // for recording the status of each child.
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

// Return a string representing the prompt to display to the user. It needs to
// include the current working directory and should also use the return value to
// indicate the result (success or failure) of the last command.
string get_prompt(int return_value)
{
    string emocon = return_value ? ":(" : ":)";
    cout << emocon << endl;
    return pwd() + " $ ";  // replace with your own code
}

// Return one of the matches, or nullptr if there are no more.
char *pop_match(vector<string> &matches)
{
    if (matches.size() > 0)
    {
        const char *match = matches.back().c_str();

        // Delete the last element
        matches.pop_back();

        // We need to return a copy, because readline deallocates when done
        char *copy = (char *) malloc(strlen(match) + 1);
        strcpy(copy, match);

        return copy;
    }

    // No more matches
    return nullptr;
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

// Generates environment variables for readline completion. This function will
// be called multiple times by readline and will return a single cstring each
// time.
char *environment_completion_generator(const char *text, int state)
{
    // A list of all the matches;
    // Must be static because this function is called repeatedly
    static vector<string> matches;

    // If this is the first time called, construct the matches list with
    // all possible matches
    if (state == 0)
    {

    }

    // Return a single match (one for each time the function is called)
    return pop_match(matches);
}

// Generates commands for readline completion. This function will be called
// multiple times by readline and will return a single cstring each time.
char *command_completion_generator(const char *text, int state)
{
    // A list of all the matches;
    // Must be static because this function is called repeatedly
    static vector<string> matches;

    // If this is the first time called, construct the matches list with
    // all possible matches
    if (state == 0)
    {

    }

    // Return a single match (one for each time the function is called)
    return pop_match(matches);
}

// This is the function we registered as rl_attempted_completion_function. It
// attempts to complete with a command, variable name, or filename.
char **word_completion(const char *text, int start, int end)
{
    char **matches = nullptr;

    if (start == 0)
    {
        rl_completion_append_character = ' ';
        matches = rl_completion_matches(text, command_completion_generator);
    }
    else if (text[0] == '$')
    {
        rl_completion_append_character = ' ';
        matches = rl_completion_matches(text, environment_completion_generator);
    }
    else
    {
        rl_completion_append_character = '\0';
        // We get directory matches for free (thanks, readline!)
    }

    return matches;
}

// Transform a C-style string into a C++ vector of string tokens, delimited by
// whitespace.
vector<string> tokenize(const char *line)
{
    vector<string> tokens;
    string token;

    // istringstream allows us to treat the string like a stream
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

            if (getenv(var_name.c_str()) != nullptr)
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

// Examines each token and sets an env variable for any that are in the form
// of key=value.
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

// Manipulate tokens if the command is !! or !#
void tokennizeForSpecialHistory(vector<string> &tokens, char *line)
{
    string tmpline = "";

    event_list.pop_back();

    // If the size of the history is 0 then do nothing.
    if (event_list.size() == 0)
    {
        cout << "There are no recent events." << endl;
        return;
    }

    if (strcmp(line, "!!") == 0)
    {
        // Access the last command which is -2 because !! is added to the last.
        tmpline = event_list[event_list.size() - 1];
        // Add this command to the history;
        event_list.push_back(tmpline);
        cout << tmpline << endl;
        tokens = tokenize(tmpline.c_str());
    }
    else if (line[0] == '!')
    {
        // Parse number from the readin.
        string number;

        for (int i = 1; i < strlen(line); i++)
        {
            number += line[i];
        }
        // Convert the string to number.
        int index = atoi(number.c_str());

        // Check if the even is in the list.
        if (index >= event_list.size() || index == 0)
        {
            cout << "!" << index << ": event not found" << endl;
        }
        else
        {
            // Access the !index
            tmpline = event_list[index - 1];
            // Add this command to the history.
            event_list.push_back(tmpline);
            cout << tmpline << endl;
            tokens = tokenize(tmpline.c_str());
        }
    }
}

// The main program
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

    // Specify the characters that readline uses to delimit words
    rl_basic_word_break_characters = (char *) WORD_DELIMITERS;

    // Tell the completer that we want to try completion first
    rl_attempted_completion_function = word_completion;

    // The return value of the last command executed
    int return_value = 0;
    // Loop for multiple successive commands

    while (true)
    {
        // Get the prompt to show, based on the return value of the last command
        string prompt = get_prompt(return_value);

        // Read a line of input from the user
        char *line = readline(prompt.c_str());

        // If the pointer is nullptr, then an EOF has been received (ctrl-d)
        if (!line)
        {
            break;
        }

        // If the command is non-empty, attempt to execute it
        if (line[0])
        {
            add_history(line);

            // Add this command to readline's history
            event_list.push_back(line);

            // Break the raw input line into tokens
            vector<string> tokens = tokenize(line);

            // Check if the command is in the alias.
            // If so replace the tokens with the commands
            map<string, vector<string> >::iterator aliasIter =
                    alias_list.find(tokens[0]);
            if (aliasIter != alias_list.end())
            {
                tokens = aliasIter->second;
            }

            // Check if the command is special history commands.
            if (strcmp(line, "!!") == 0 || line[0] == '!')
            {
                tokennizeForSpecialHistory(tokens, line);
            }

            // Handle local variable declarations
            local_variable_assignment(tokens);

            // Substitute variable references
            variable_substitution(tokens);

            // Execute the line
            return_value = execute_line(tokens, builtins);
        }

        // Free the memory for the input string
        free(line);
    }

    return 0;
}
