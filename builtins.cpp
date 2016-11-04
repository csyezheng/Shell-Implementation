#include "builtins.h"
#include <map>
#include <readline/history.h>

using namespace std;

vector<string> event_list;
map<string, vector<string> > alias_list;

int com_ls(vector<string> &tokens)
{
    // if no directory is given, use the local directory
    if (tokens.size() < 2)
    {
        tokens.push_back(".");
    }

    // open the directory
    DIR *dir = opendir(tokens[1].c_str());

    // catch an errors opening the directory
    if (!dir)
    {
        // print the error from the last system call with the given prefix
        perror("ls error: ");

        // return error
        return 1;
    }

    // output each entry in the directory
    for (dirent *current = readdir(dir); current; current = readdir(dir))
    {
        cout << current->d_name << endl;
    }

    // return success
    return 0;
}

int com_cd(vector<string> &tokens)
{
    chdir(tokens[1].c_str());
    return 0;
}

int com_pwd(vector<string> &tokens)
{
    // HINT: you should implement the actual fetching of the current directory in
    // pwd(), since this information is also used for your prompt
    // cout << "pwd called" << pwd() << endl; // delete when implemented
    cout << pwd() << endl;
    return 0;
}

int com_alias(vector<string> &tokens)
{
    // List all aliases if the size of tokesn is 1.
    // Map iterator for the alias_list
    map<string, vector<string> >::iterator iter;

    if (tokens.size() == 1)
    {
        for (iter = alias_list.begin(); iter != alias_list.end(); iter++)
        {
            string value = "";
            // Build the command from the vector.
            for (vector<string>::iterator vectorIter = iter->second.begin();
                 vectorIter != iter->second.end(); vectorIter++)
            {
                value += *vectorIter + " ";
            }
            // Remove the last space.
            value = value.substr(0, value.length() - 1);

            cout << "alias " << iter->first << "='" << value << "'" << endl;
        }
    }
    else
    {
        // Add the alias to the alias list.
        string key = tokens[1].substr(0, tokens[1].find("="));
        string cmd = tokens[1].substr(tokens[1].find("=") + 1, tokens[1].length());
        vector<string> tmp;

        tmp.push_back(cmd);

        for (int i = 2; i < tokens.size(); i++)
        {
            tmp.push_back(tokens[i]);
        }

        alias_list[key] = tmp;
        tmp.clear();
    }
    return 0;
}

int com_unalias(vector<string> &tokens)
{
    // Remove the key.
    if (tokens.size() > 2)
    {
        cout << "Unalias can't contain both -a and an alias name." << endl;
        return 1;
    }
    else
    {
        if (tokens[1] == "-a")
        {
            // Clear the alias_list contents.
            alias_list.clear();
        }
        else
        {
            // Erase the key from the alias_list.
            alias_list.erase(tokens[1]);
        }
    }
    return 0;
}

int com_echo(vector<string> &tokens)
{
    string echo;
    for (int i = 1; i < tokens.size(); i++)
    {
        echo += tokens[i] + " ";
    }
    echo = echo.substr(0, echo.length() - 1);
    cout << echo << endl;
    return 0;
}

int com_exit(vector<string> &tokens)
{
    exit(0);
    return 0;
}

int com_history(vector<string> &tokens)
{
    for (int i = 1; i < event_list.size() + 1 && i < 30; i++)
    {
        cout << "  " << i << "  " << event_list[i - 1] << endl;
    }
    return 0;
}

// update the history list
void update_history(string command)
{ event_list.push_back(command); }

string pwd()
{
    char the_path[260];
    getcwd(the_path, 259);
    return the_path;
}
