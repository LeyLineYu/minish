# minish

`minish` is a very minimalistic shell implementation

## Installation and build
```bash
git clone git@github.com:LeyLineYu/minish.git
cd minish
./build.sh
```

## Usage

`minish` has two modes

**The first one is called a "one-off mode":**

```bash
./build/minish cmd arg1 arg2 arg3 arg4
```
`minish` will execute `cmd` with the provided `arg`s. 
In this mode, piping is not implemented, since this just redirects
the arguments straight to execution, with no parsing involved.

**The second mode is called an "interactive mode":**

To launch this mode, provide no arguments to `minish`.
```bash
./build/minish
```

You'll be greeted with a prompt in which you can type commands and their arguments, 
separated either by pipes (`|`) or whitespace. After you type your prompt, `minish`
will execute the commands and pipe them if needed. 

To exit the interactive mode, use either Keyboard Interruption or type
```bash
$ exit
```
as your prompt.
