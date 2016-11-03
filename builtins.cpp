//
// Created by csyezheng on 11/3/16.
//

#include "builtins.h"
#include <map>
//  #include <readline/history.h>

using namespace std;

vector<string> event_list;
map<string, string> alias_list;

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
    cout << pwd() << endl;
    return 0;
}

int com_alias(vector<string> &tokens)
{
    if (tokens.size() == 1)                         // List all aliases
    {
        for (auto iter = alias_list.begin(); iter != alias_list.end(); iter++)
        {
            cout << "alias " << iter->first << "='" << iter->second << "'" << endl;
        }
    }
    else                                           // Add the alias to the alias list.
    {
        string key = tokens[1].substr(0, tokens[1].find("="));
        string cmd = tokens[1].substr(tokens[1].find("=") + 1, tokens[1].length());
        alias_list[key] = cmd;
    }
    return 0;
}

int com_unalias(vector<string> &tokens)                     // Remove the alias
{
    if (tokens.size() < 2)
    {
        cout << "Unalias usage: unalias [-a] name..." << endl;
        return 1;
    }
    else
    {
        if (tokens[1] == "-a")
        {
            alias_list.clear();
        }
        else
        {
            for (auto iter = tokens.begin() + 1; iter != tokens.end(); ++iter)
            {
                auto ret = alias_list.find(*iter);
                if (ret != alias_list.end())
                    alias_list.erase(*iter);
                else
                    cout << "bash: unalias: " << *iter << ": not found" << endl;
            }
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

string pwd()
{
    char the_path[260];
    getcwd(the_path, 259);
    return the_path;
}
