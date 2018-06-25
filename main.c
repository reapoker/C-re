#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <string.h>
#include <fcntl.h>

int debug;//调试模式
int assembly;//分部模式，列出每步对应的汇编代码

int token,//当前的标记
        token_val,//当前的标记值
        line;//行号
//指向目标代码的主函数
int *idmain;
//用于读取源代码字符串
char *src, *old_src;
int poolsize;//分配内存的大小
//声明类型、表达式类型
int basetype,expr_type;

//token标记
/**
 * 用枚举类型声明所有常量，声明的常量被枚举成数字，
 * 用指针加下标的形式访问开辟出来的一段内存，模拟结构体类型
 **/
//标识符表，每个token的属性
enum { Token, Hash, Name, Type, Class, Value, BType, BClass, BValue, IdSize};
/**
 * token：该标识符返回的标记，理论上所有的变量返回的标记都应该是 Id，但实际上由于我们还将在符号表中加入关键字如 if, while 等，它们都有对应的标记。
 * hash：顾名思义，就是这个标识符的哈希值，用于标识符的快速比较。
 * name：存放标识符本身的字符串。
 * class：该标识符的类别，如数字，全局变量或局部变量等。
 * type：标识符的类型，即如果它是个变量，变量是 int 型、char 型还是指针型。
 * value：存放这个标识符的值，如标识符是函数，刚存放函数的地址。
 * BXXXX：C 语言中标识符可以是全局的也可以是局部的，当局部标识符的名字与全局标识符相同时，用作保存全局标识符的信息。
 * IdSize: 因为枚举本身就是从1开始，所以IdSize就正好是一个token的大小
 * */
//标记表，即可识别的token
enum { Num=128, Fun, Sys, Glo, Loc, Id, Char, Else, Enum, If, Int, Return,
    Sizeof, While, Assign, Cond, Lor, Lan, Or, Xor, And, Eq, Ne, Lt, Gt,
    Le, Ge, Shl, Shr, Add, Sub, Mul, Div, Mod, Inc, Dec, Brak };
/**
 * C-re所支持的输入源码的字符集限定在7-bit ASCII上所以输入的字符只可能在[0,127]的闭区间范围内，
 * 所以单字符的token可以直接用ASCII码表示其token类别，而对于多字符token(如关键字、内置函数)则用
 * 大于ASCII码的数字来表示其token类别，所以以上标记表从128开始
 * */
//变量和函数的类型
enum { CHAR, INT, PTR };
//作用域类型
enum { Global, Local };
//当前的标记id，指向标识符表
int *current_id, *symbols;
//栈指针的索引，用于函数调用
int index_of_bp;

//虚拟机
/**虚拟机三个基本部件：CPU、寄存器及内存。
 * 代码（汇编指令）以二进制的形式保存在内存中，
 * CPU 从中一条条地加载指令执行。
 * 程序运行的状态保存在寄存器中。
 */
//虚拟机的内存部分
int *text, *old_text, *stack;
char *data;
/**
 * 代码段（text）用于存放代码（指令）。
 * 数据段（data）用于存放初始化了的数据，如int i = 10;，就需要存放到数据段中。
 * 栈（stack）用于处理函数调用相关的数据，如调用帧（calling frame）或是函数的局部变量等。
 * */
//虚拟机的寄存器部分
int *pc, *bp, *sp, ax, cycle;
/**
 * PC 程序计数器，它存放的是一个内存地址，该地址中存放着 下一条 要执行的计算机指令。
 * SP 指针寄存器，永远指向当前的栈顶。注意的是由于栈是位于高地址并向低地址增长的，所以入栈时 SP 的值减小。
 * BP 基址指针。也是用于指向栈的某些位置，在调用函数时会使用到它。
 * AX 通用寄存器，我们的虚拟机中，它用于存放一条指令执行后的结果。
 * */
//虚拟机中的指令集
enum { LEA, IMM, JMP, CALL, JZ, JNZ, ENT, ADJ, LEV, LI, LC, SI, SC, PUSH,
    OR, XOR, AND, EQ, NE, LT, GT, LE, GE, SHL, SHR, ADD, SUB, MUL, DIV, MOD,
    OPEN, READ, CLOS, PRTF, MALC, MSET, MCMP, EXIT };


//词法分析
void next(){
    char *last_pos;
    int hash;
    while (token=*src){//此时的token并不是真正的token，而是直接读取的源码
        //跳过那些不能识别的字符，同时也用来处理空白字符，
        // 也就是说，将那些未识别的字符当成空白字符跳过
        ++src;
        if (token=='\n'){
            if (assembly){//分部模式
                printf("%d: %.*s", line, src-old_src, old_src);
                old_src = src;
                while (old_text < text) {//在google上没搜到，自己测试了一下，
                    // 其实就是定义一个字符串，因为C的特性，会把分开的字符串拼接，
                    // 然后用&定义一个位于首地址的指针，并用下标偏移量来截取字符个数为4的字符串
                    printf("%8.4s", & "LEA ,IMM ,JMP ,CALL,JZ  ,JNZ ,ENT ,ADJ ,LEV ,LI  ,LC  ,SI  ,SC  ,PUSH,"
                                      "OR  ,XOR ,AND ,EQ  ,NE  ,LT  ,GT  ,LE  ,GE  ,SHL ,SHR ,ADD ,SUB ,MUL ,DIV ,MOD ,"
                                      "OPEN,READ,CLOS,PRTF,MALC,MSET,MCMP,EXIT"[*++old_text * 5]);

                    if (*old_text <= ADJ)
                        printf(" %d\n", *++old_text);
                    else
                        printf("\n");
                }
            }
            ++line;
        } else if (token=='#'){//跳过头文件导入，将内置函数直接插入symbol表
            while (*src!=0 && *src!='\n'){
                src++;
            }
        } else if ((token>='a' && token<='z')||(token>='A' && token<='Z')||(token=='_')){//字符串表示
            last_pos=src-1;//因为之前src已经++过，所以这里的last_pos就是当前的token
            hash=token;
            //向前看一个字符，若符合要求，则判定与之前的为同一字符串，并计算哈希值
            while ((*src>='a' && *src<='z')||(*src>='A' && *src<='Z')||(*src>='0' && *src<='9')||(*src=='_')){
                hash=hash*147+*src;//一个线性相关的哈希求值
                src++;
            }
            //线性查询已存在的标识，使current_id从symbol table的开头开始遍历
            current_id=symbols;
            while (current_id[Token]){//token存在即不为零
                if (current_id[Hash]==hash && !memcmp((char*)current_id[Name],last_pos,src-last_pos)){
                    //若找到则直接返回
                    token=current_id[Token];//真实token
                    return;
                }
                current_id=current_id+IdSize;//查询下一个单位
            }
            //若没有找到现有的token，则插入表中
            current_id[Name]=(int)last_pos;
            current_id[Hash]=hash;
            token=current_id[Token]=Id;//真实token
            return;
        } else if (token>='0' && token<='9'){//数字表示
            token_val=token-'0';//首位字符串转数字
            if (token_val>0){//首位大于零，十进制表示
                while (*src>='0' && *src<='9'){
                    token_val=token_val*10 + *src++ -'0';
                }
            } else{
                if (*src=='x'||*src=='X'){//0x...||0X... 十六进制
                    token=*++src;
                    while ((token>='0' && token<='9')||(token>='a' && token<='f')||(token>='A' && token<='F')){
                        token_val=token_val*16+(token&15)+(token>='A'?9:0);//十六进制计算法
                        token=*++src;
                    }
                } else{//0... 八进制
                    while (*src>='0' && *src<='7'){
                        token_val=token_val*8+*src++ -'0';
                    }
                }
            }
            token=Num;
            return;
        } else if (token=='/'){//注释或除号
            if (*src=='/'){
                while (*src!=0 && *src!='\n'){
                    src++;
                }
            } else{
                token=Div;
                return;
            }
        } else if (token=='"'||token=='\''){//解析""和''字符串文字，仅支持'\n'转义字符串,字符串存到数据段data中
            last_pos=data;//指向data
            while (*src!=0 && *src!=token){//无换行
                token_val=*src++;//若为单字符，此时存值
                if (token_val=='\\'){//字符串中出现'\'时
                    token_val=*src++;
                    if (token_val=='n'){
                        token_val='\n';
                    }
                }
                if (token=='"'){
                    *data++ =token_val;
                }
            }
            src++;//继续向下进行
            if (token=='"'){
                token_val=(int)last_pos;
            } else{
                token=Num;//若为单字符则返回Num
            }
            return;
        } else if (token=='='){
            if (*src=='='){//判断是否相等
                src++;
                token=Eq;
            } else{//赋值
                token=Assign;
            }
            return;
        } else if (token=='+'){
            if (*src=='+'){//自增
                src++;
                token=Inc;
            } else{
                token=Add;
            }
            return;
        } else if (token=='-'){
            if (*src=='-'){//自减
                src++;
                token=Dec;
            } else{
                token=Sub;
            }
            return;
        } else if (token=='!'){
            if (*src=='='){// !=
                src++;
                token=Ne;
            }//token='!'
            return;
        } else if (token=='<'){
            if (*src=='='){//<=
                src++;
                token=Le;
            } else if (*src=='<'){//<<
                src++;
                token=Shl;
            } else{//<
                token=Lt;
            }
            return;
        } else if (token=='>'){
            if (*src=='='){//>=
                src++;
                token=Ge;
            } else if (*src=='>'){//>>
                src++;
                token=Shr;
            } else{//>
                token=Gt;
            }
            return;
        } else if (token=='|'){
            if (*src=='|'){//||
                src++;
                token=Lor;
            } else{//|
                token=Or;
            }
            return;
        } else if (token=='&'){
            if (*src=='&'){//&&
                src++;
                token=Lan;
            } else{//&
                token=And;
            }
            return;
        } else if (token=='^'){
            token=Xor;
            return;
        } else if (token=='%'){
            token=Mod;
            return;
        } else if (token=='*'){
            token=Mul;
            return;
        } else if (token=='['){
            token=Brak;
            return;
        } else if (token=='?'){
            token==Cond;
            return;
        } else if (token=='~'||token==';'||token=='{'||token=='}'||token=='('||token==')'
                   ||token==']'||token==','||token==':'){
            return;//token就是本身
        }
    }
}

//匹配当前token，移向下一个token
void match(int tk){
    if (token==tk){
        next();
    } else{
        printf("%d: expected token: %d\n",line,tk);
        exit(-1);
    }
}

//解析表达式
void expression(int level){
    /**
     * 表达式主要有两部分，单元和运算符
     * 表达式有以下类型
     * 1. unit_unary ::= unit | unit unary_op | unary_op unit
     * 2. expr ::= unit_unary (bin_op unit_unary ...)
     * */

    //unit_unary()
    int *id;
    int tmp;
    int *addr;
    {
        if (!token){
            printf("%d: unexpexted token EOF of expression\n",line);
            exit(-1);
        }
        if (token==Num){
            //加载数字常量
            match(Num);
            //记录代码
            *++text=IMM;
            *++text=token_val;
            expr_type=INT;
        } else if (token == '"') {
            //加载字符串常量,记录代码
            *++text = IMM;
            *++text = token_val;

            match('"');
            //储存剩余的字符串
            while (token == '"') {
                match('"');
            }
            //默认在字符串的末尾添加'\n'，对于0，就仅仅移一个相对位置即可
            data = (char *)(((int)data + sizeof(int)) & (-sizeof(int)));
            expr_type = PTR;
        } else if (token==Sizeof){
            //sizeof是一元运算符，需要知道运算类型，且仅支持int、char、指针
            match(Sizeof);
            match('(');
            expr_type=INT;//此处expr_type临时表示参数的类型
            if (token==Int){
                match(Int);
            } else if (token==Char){
                match(Char);
                expr_type=CHAR;
            }
            while (token==Mul){
                match(Mul);
                expr_type=expr_type+PTR;
            }
            match(')');
            //记录代码
            *++text=IMM;
            *++text=(expr_type==CHAR)? sizeof(char): sizeof(int);

            expr_type=INT;
        } else if (token==Id){
            //变量与函数调用都以Id开始
            match(Id);
            id=current_id;
            if (token=='('){
                //函数调用
                match('(');
                tmp=0;//参数数量
                while (token!=')'){
                    //参数顺序入栈
                    expression(Assign);
                    *++text=PUSH;
                    tmp++;
                    if (token==','){
                        match(',');
                    }
                }
                match(')');
                //内置函数直接调用汇编指令，
                // 而普通的函数则编译成 CALL <addr> 的形式。
                if (id[Class]==Sys){
                    //内置函数
                    *++text=id[Value];
                } else if (id[Class]==Fun){
                    //自定义函数
                    *++text=CALL;
                    *++text=id[Value];
                } else{
                    printf("%d: bad function call\n",line);
                    exit(-1);
                }
                //清除入栈的参数。因为我们不在乎出栈的值，所以直接修改栈指针的大小即可。
                if (tmp>0){
                    *++text=ADJ;
                    *++text=tmp;
                }
                expr_type=id[Type];
            } else if (id[Class]==Num){
                //当该标识符是全局定义的枚举类型时，直接将对应的值用 IMM 指令存入 AX 即可。
                *++text=IMM;
                *++text=id[Value];
                expr_type=INT;
            } else {
                //加载变量的值，如果是局部变量则采用与 bp 指针相对位置的形式
                // 而如果是全局变量则用 IMM 加载变量的地址。
                if (id[Class]==Loc){
                    //局部变量
                    *++text=LEA;
                    *++text=index_of_bp-id[Value];
                } else if (id[Class==Glo]){
                    //全局变量
                    *++text=IMM;
                    *++text=id[Value];
                } else{
                    printf("%d: undefined variable\n",line);
                    exit(-1);
                }
                //无论是全局还是局部变量，最终都根据它们的类型用 LC 或 LI 指令加载对应的值
                expr_type=id[Type];
                *++text=(expr_type==Char)?LC:LI;
            }
        } else if (token=='('){
            //撇或括号
            match('(');
            if (token==Int||token==Char){
                //强制转换
                tmp=(token==Char)?CHAR:INT;
                match(token);
                while (token==Mul){
                    match(Mul);
                    tmp=tmp+PTR;
                }
                match(')');
                expression(Inc);
                expr_type=tmp;
            } else{
                //正常括号
                expression(Assign);
                match(')');
            }
        } else if (token==Mul){
            //指针取值
            match(Mul);
            expression(Inc);
            if (expr_type>=PTR){
                expr_type=expr_type-PTR;
            } else{
                printf("%d: bad dereference\n",line);
                exit(-1);
            }
            *++text=(expr_type==CHAR)?LC:LI;
        } else if (token==And){
            //取值操作
            //对于变量先加载它的地址，并根据它们类型使用 LC/LI 指令加载实际内容
            //这里只要不执行LC/LI操作即可
            match(And);
            expression(Inc);//获取地址
            if (*text==LC||*text==LI){
                text--;
            } else{
                printf("%d: bad address of\n",line);
                exit(-1);
            }
            expr_type=expr_type+PTR;
        } else if (token=='!'){
            //逻辑取反
            match('!');
            expression(Inc);
            *++text=PUSH;
            *++text=IMM;
            *++text=0;
            *++text=EQ;
            expr_type=INT;
        } else if (token=='~'){
            //按位取反
            match('~');
            expression(Inc);
            *++text=PUSH;
            *++text=IMM;
            *++text=-1;
            *++text=XOR;
            expr_type=INT;
        } else if (token==Add){
            //四则运算中的加减法，不是单个数字的取正取负操作
            match(Add);
            expression(Inc);
            expr_type=INT;
        } else if (token==Sub){
            match(Sub);
            if (token==Num){
                *++text=IMM;
                *++text=-token_val;
                match(Num);
            } else{
                *++text=IMM;
                *++text=-1;
                *++text=PUSH;
                expression(Inc);
                *++text=MUL;
            }
            expr_type=INT;
        } else if (token==Inc||token==Dec){
            //自增自减
            tmp=token;
            match(token);
            expression(Inc);
            if (*text==LC){
                *text=PUSH;//因为需要用到两次地址，所以先PUSH一下
                *++text=LC;
            } else if (*text==LI){
                *text=PUSH;
                *++text=LI;
            } else{
                printf("%d: bad lvalue of pre-increment\n",line);
                exit(-1);
            }
            *++text=PUSH;
            *++text=IMM;
            *++text=(expr_type>PTR)? sizeof(int): sizeof(char);//考虑指针的情况
            *++text=(tmp==Inc)?ADD:SUB;
            *++text=(expr_type==CHAR)?SC:SI;
        } else{
            printf("%d: bad expression\n",line);
            exit(-1);
        }
    }

    //二元运算符和三元运算符
    /**
     * 运用了递归下降和运算符优先级混合的方法
     * 这里运用了两个栈，来实现优先级的运算，一个用于储存运算符优先级，一个用于储存中间的计算结果
     * 处理运算符的栈通过函数的递归调用隐含在函数调用栈里，而储存中间计算结果的的栈则位于虚拟机中
     * 因为编译器生成的目标代码是虚拟机中的字节码，而虚拟机中的指令集本就是基于栈的，
     * 所以虚拟机中的指令集在表达式部分其实就是一种后缀表达式
     * */
    {
        while (token>=level){
            //在调用expression时，通过参数level定义了当前的优先级
            // 只有当前运算符优先级大于level时才会继续递归调用下去
            tmp=expr_type;
            if (token==Assign){
                //赋值运算是当前优先级最低的运算符
                //当解析完=右边的表达式后，相应的值会存放在 ax 中，用PUSH实际将这个值保存起来
                match(Assign);
                if (*text==LC||*text==LI){
                    *text=PUSH;//保存运算后的右值
                } else{
                    printf("%d: bad lvalue in assignment\n",line);
                    exit(-1);
                }
                expression(Assign);
                expr_type=tmp;
                *++text=(expr_type==CHAR)?SC:SI;
            } else if (token==Cond){
                //expr?a:b 唯一的三目运算符，相当于一个小型的if语句
                match(Cond);
                *++text=JZ;
                addr=++text;
                expression(Assign);
                if (token==':'){
                    match(':');
                } else{
                    printf("%d: missing colon in conditional\n",line);
                    exit(-1);
                }
                *addr=(int)(text+3);
                *++text=JMP;
                addr=++text;
                expression(Cond);
                *addr=(int)(text+1);
            }
                /**
                         * <expr1> || <expr2>     <expr1> && <expr2>
                         * ...<expr1>...          ...<expr1>...
                         * JNZ b                  JZ b
                         * ...<expr2>...          ...<expr2>...
                         * b:                     b:
                         * */
            else if (token==Lor){
                //逻辑或
                match(Lor);
                *++text=JNZ;
                addr=++text;
                expression(Lan);
                *addr=(int)(text+1);
                expr_type=INT;
            } else if (token==Lan){
                //逻辑与
                match(Lan);
                *++text=JZ;
                addr=++text;
                expression(Or);
                *addr=(int)(text+1);
                expr_type=INT;
            }
                /**
                 * 数学运算符，包括 |, ^, &, ==, != <=, >=, <, >, <<, >>, +, -, *, /, %
                 * */
            else if (token==Or){
                //||
                match(Or);
                *++text=PUSH;
                expression(Xor);
                *++text=OR;
                expr_type=INT;
            } else if (token==Xor){
                //^
                match(Xor);
                *++text=PUSH;
                expression(And);
                *++text=XOR;
                expr_type=INT;
            } else if (token==And){
                //&&
                match(And);
                *++text=PUSH;
                expression(Eq);
                *++text=AND;
                expr_type=INT;
            } else if (token==Eq){
                //==
                match(Eq);
                *++text=PUSH;
                expression(Ne);
                *++text=EQ;
                expr_type=INT;
            } else if (token==Ne){
                // !=
                match(Ne);
                *++text=PUSH;
                expression(Lt);
                *++text=NE;
                expr_type=INT;
            } else if (token==Lt){
                //<
                match(Lt);
                *++text=PUSH;
                expression(Shl);
                *++text=LT;
                expr_type=INT;
            } else if (token==Gt){
                //>
                match(Gt);
                *++text=PUSH;
                expression(Shl);
                *++text=GT;
                expr_type=INT;
            } else if (token==Le){
                //<=
                match(Le);
                *++text=PUSH;
                expression(Shl);
                *++text=LE;
                expr_type=INT;
            } else if (token==Ge){
                //>=
                match(Ge);
                *++text=PUSH;
                expression(Shl);
                *++text=GE;
                expr_type=INT;
            } else if (token==Shl){
                //<<
                match(Shl);
                *++text=PUSH;
                expression(Add);
                *++text=SHL;
                expr_type=INT;
            } else if (token==Shr){
                //>>
                match(Shr);
                *++text=PUSH;
                expression(Add);
                *++text=SHR;
                expr_type=INT;
            } else if (token==Add){
                //+，注意这里的+操作还有可能是对于指针的
                match(Add);
                *++text=PUSH;
                expression(Mul);
                expr_type=tmp;
                if(expr_type>PTR){
                    //指针类型，不包括char*
                    *++text=PUSH;
                    *++text=IMM;
                    *++text= sizeof(int);
                    *++text=MUL;
                }
                *++text=ADD;
            } else if (token==Sub){
                //-,同样有指针操作
                match(Sub);
                *++text=PUSH;
                expression(Mul);
                if (tmp>PTR && tmp==expr_type){
                    //指针减法
                    *++text=SUB;
                    *++text=PUSH;
                    *++text=IMM;
                    *++text= sizeof(int);
                    *++text=DIV;
                    expr_type=INT;
                } else if (tmp>PTR){
                    //指针
                    *++text=PUSH;
                    *++text=IMM;
                    *++text= sizeof(int);
                    *++text=MUL;
                    *++text=SUB;
                    expr_type=tmp;
                } else{
                    //普通减法
                    *++text=SUB;
                    expr_type=tmp;
                }
            } else if (token==Mul){
                //*
                match(Mul);
                *++text=PUSH;
                expression(Inc);
                *++text=MUL;
                expr_type=tmp;
            } else if(token==Div){
                // /
                match(Div);
                *++text=PUSH;
                expression(Inc);
                *++text=DIV;
                expr_type=tmp;
            } else if (token==Mod){
                //%
                match(Mod);
                *++text=PUSH;
                expression(Inc);
                *++text=MOD;
                expr_type=tmp;
            } else if (token==Inc||token==Dec){
                //后缀形式的自增自减
                /**
                 * 与前缀形式不同的是，在执行自增自减后， ax上需要保留原来的值。
                 * 所以我们首先执行类似前缀自增自减的操作，再将 ax 中的值执行减/增的操作。
                 * */
                if (*text==LI){
                    *text=PUSH;
                    *++text=LI;
                } else if(*text==LC){
                    *++text=PUSH;
                    *++text=LC;
                } else{
                    printf("%d: bad value in increment\n",line);
                    exit(-1);
                }
                *++text=PUSH;
                *++text=IMM;
                *++text=(expr_type>PTR)? sizeof(int): sizeof(char);
                *++text=(token==Inc)?ADD:SUB;
                *++text=(expr_type==CHAR)?SC:SI;
                *++text=PUSH;
                *++text=IMM;
                *++text=(expr_type>PTR)? sizeof(int): sizeof(char);
                *++text=(token==Inc)?SUB:ADD;
                match(token);
            } else if (token==Brak){
                //数组取值操作，a[10]=*(a+10)
                match(Brak);
                *++text=PUSH;
                expression(Assign);
                match(']');
                if (tmp>PTR){
                    //不含char的指针类型
                    *++text=PUSH;
                    *++text=IMM;
                    *++text= sizeof(int);
                    *++text=MUL;
                } else if (tmp<PTR){
                    printf("%d: pointer type expected\n",line);
                    exit(-1);
                }
                expr_type=tmp-PTR;
                *++text=ADD;
                *++text=(expr_type==CHAR)?LC:LI;
            } else{
                printf("%d: compiler error, token=%d\n",line,token);
                exit(-1);
            }
        }
    }
}

//解析语句块
void statement(){
    /**
     * 在我们的编译器中共识别 6 种语句：
     * if (...) <statement> [else <statement>]
     * while (...) <statement>
     * { <statement> }
     * return xxx;
     * <empty statement>;
     * expression; (expression end with semicolon)
     * */
    int *a, *b;//用于控制分支
    if (token==If){
        /**
         * if (...) <statement> [else <statement>]
         * if (<cond>)                   <cond>
         *                               JZ a
         * <true_statement>   ===>     <true_statement>
         * else:                         JMP b
         * a:                           a:
         * <false_statement>           <false_statement>
         * b:                           b:
         * */
        match(If);
        match('(');
        expression(Assign);
        match(')');
        *++text=JZ;//跳转
        b=++text;//指向当前的text的指针
        statement();//解析{<statement>}
        if (token==Else){
            match(Else);
            *b=(int)(text+3);
            *++text=JMP;
            b=++text;
            statement();
        }
        *b=(int)(text+1);
    } else if (token==While){
        /**
         a:                     a:
            while (<cond>)        <cond>
                                  JZ b
             <statement>          <statement>
                                  JMP a
         b:                     b:
         */
        match(While);
        a=text+1;
        match('(');
        expression(Assign);
        match(')');
        *++text=JZ;
        b=++text;
        statement();
        *++text=JMP;
        *++text=(int)a;
        *b=(int)(text+1);
    } else if (token=='{'){
        // { <statement> ... }
        match('{');
        while (token!='}'){
            statement();
        }
        match('}');
    } else if (token==Return){
        // return [expression];
        match(Return);
        if (token!=';'){
            expression(Assign);
        }
        match(';');
        *++text=LEV;//跳出函数体
    } else if (token==';'){
        //空语句
        match(';');
    } else{
        //a=b;或 function_call
        expression(Assign);
        match(';');
    }
}

//枚举类型声明语句,主要的逻辑用于解析用逗号（,）分隔的变量，将该变量的类别设置为Num，使之成为全局变量
void enum_declaration(){
    int i=0;
    while (token!='}'){
        if (token!=Id){
            printf("%d: bad enum identifier %d\n",line,token);
            exit(-1);
        }
        next();
        if (token==Assign){//如果是赋值语句
            next();
            if (token!=Num){
                printf("%d: bad enum initializer\n",line);
                exit(-1);
            }
            i=token_val;//若有赋值操作，则将i赋予新值
            next();
        }
        current_id[Class]=Num;
        current_id[Type]=INT;
        current_id[Value]=i++;//无赋值则++
        if (token==','){
            next();
        }
    }
}

//解析函数参数
void function_parameter(){
    int type,params=0;//params为参数个数
    //匹配类型
    while (token!=')'){
        type=INT;//默认为int
        if (token==Int){
            match(Int);
        } else if (token==Char){
            type=CHAR;
            match(Char);
        }
        //匹配指针
        while (token==Mul){
            match(Mul);
            type+=PTR;
        }
        //匹配参数名
        if (token!=Id){
            printf("%d: bad parameter declaration\n",line);
            exit(-1);
        }
        if (current_id[Class]==Loc){//若此局部变量已被声明
            printf("%d: duplicate parameter declaration\n",line);
            exit(-1);
        }
        match(Id);
        //储存形参局部变量，将重名的全局变量备份到Bxxx系列，当出函数体时，再还原
        current_id[BClass]=current_id[Class];current_id[Class]=Loc;
        current_id[BType]=current_id[Type];current_id[Type]=type;
        current_id[BValue]=current_id[Value];current_id[Value]=params++;//作为当前参数的索引
        if (token==','){
            match(',');
        }
    }
    index_of_bp=params+1;
}

//解析函数体，函数体内必须遵循先声明在调用原则，即先声明所有变量，再进行调用
void function_body(){
    int pos_local,type;
    pos_local=index_of_bp;
    //解析局部变量声明
    while (token==Int||token==Char){
        basetype=(token==Int)?INT:CHAR;
        match(token);
        while (token!=';'){
            type=basetype;
            while (token==Mul){
                match(Mul);
                type=type+PTR;
            }
            if (token!=Id){
                printf("%d: bad local declaration\n",line);
                exit(-1);
            }
            if (current_id[Class]==Loc){
                printf("%d: duplicate local declaration\n", line);
                exit(-1);
            }
            match(Id);
            //储存局部变量
            current_id[BClass] = current_id[Class]; current_id[Class]  = Loc;
            current_id[BType]  = current_id[Type];  current_id[Type]   = type;
            current_id[BValue] = current_id[Value]; current_id[Value]  = ++pos_local;
            if (token==','){
                match(',');
            }
        }
        match(';');
    }//局部变量声明结束
    //保存局部变量在栈中的位置，并预留一些空间
    *++text=ENT;
    *++text=pos_local-index_of_bp;
    //解析语句块
    while (token!='}'){
        statement();
    }
    //释放内存
    *++text=LEV;
}

//函数声明语句，形式为：type func_name(...){...}
void function_declaration(){
    match('(');
    function_parameter();
    match(')');
    match('{');
    function_body();
    //match('}'); 末尾的'}'用于解析函数体时判断是否结束
    //遍历symbol表，恢复全局变量
    current_id=symbols;
    while (current_id[Token]){
        if (current_id[Class]==Loc){
            current_id[Class]=current_id[BClass];
            current_id[Type]=current_id[BType];
            current_id[Value]=current_id[BValue];
        }
        current_id=current_id+IdSize;
    }
}

//全局的定义语句，包括变量定义，类型定义（只支持枚举）及函数定义
void global_declaration(){
    int type, i;//临时变量，type用于保存当前类型
    basetype=INT;//默认为int类型

    //enum枚举类型语法
    if (token==Enum){
        match(Enum);
        // enum [id] { a = 10, b = 20, ... }
        if (token!='{'){
            match(Id);
        }//判断其是否有变量名
        if (token=='{'){
            match('{');
            enum_declaration();
            match('}');
        }
        match(';');
        return;
    }

    //分析类型
    if (token==Int){
        match(Int);
    } else if (token==Char){
        match(Char);
        basetype=CHAR;//修改默认类型
    }
    //分析";"分隔的语句
    while (token!=';' && token!='}'){
        type=basetype;
        while (token==Mul){//指针类型
            match(Mul);
            type=type+PTR;
        }
        if (token!=Id){
            printf("%d: bad global declaration\n",line);
            exit(-1);
        }
        if (current_id[Class]){//若标识符已经存在
            printf("%d: duplicate global declaration\n",line);
            exit(-1);
        }
        match(Id);
        current_id[Type]=type;
        if (token=='('){
            current_id[Class]=Fun;
            current_id[Value]=(int)(text+1);//记录函数的地址
            function_declaration();//解析函数声明
        } else{
            current_id[Class]=Glo;
            current_id[Value]=(int)data;
            data=data+sizeof(int);
        }
        if (token==','){
            match(',');
        }
    }
    next();
}

//语法分析
void program(){
    //获取下一个token
    next();
    while (token>0){
        global_declaration();
    }
}

//虚拟机
int eval(){
    int op, *tmp;
    cycle=0;
    while (1){
        cycle++;
        op=*pc++;//获取下一个操作指令
        //打印debug信息
        if (debug){
            printf("%d>%.4s",cycle,
                   & "LEA ,IMM ,JMP ,CALL,JZ  ,JNZ ,ENT ,ADJ ,LEV ,LI  ,LC  ,SI  ,SC  ,PUSH,"
                     "OR  ,XOR ,AND ,EQ  ,NE  ,LT  ,GT  ,LE  ,GE  ,SHL ,SHR ,ADD ,SUB ,MUL ,DIV ,MOD ,"
                     "OPEN,READ,CLOS,PRTF,MALC,MSET,MCMP,EXIT"[op * 5]);
            if (op<=ADJ)
                printf("%d\n",line);
            else
                printf("\n");
        }
        if (op == IMM)       {ax = *pc++;}//将当前的pc中值的地址存到寄存器ax中
        else if (op == LC)   {ax = *(char *)ax;}//将对应地址中的字符载入ax
        else if (op == LI)   {ax = *(int *)ax;}//将对应地址中的整数载入ax
        else if (op == SC)   {ax = *(char *)*sp++ = ax;}//sp中存放的地址，*sp取出其中的地址，转换成char类型，再次进行取址，作用同下
        else if (op == SI)   {*(int *)*sp++ = ax;}//将 ax 中的数据作为整数存放入地址中，要求栈顶存放地址
        else if (op == PUSH) {*--sp = ax;}//将ax的值入栈
        else if (op == JMP)  {pc = (int *)*pc;}//跳转到pc中存放的地址
        else if (op == JZ)   {pc = ax ? pc + 1 : (int *)*pc;}//若为1，则进行下一条指令，若为0，则跳转到对应地址
        else if (op == JNZ)  {pc = ax ? (int *)*pc : pc + 1;}//为1跳转，为0继续
        else if (op == CALL) {*--sp = (int)(pc+1); pc = (int *)*pc;}//跳转到函数所在地址
            //else if (op == RET)  {pc = (int *)*sp++;}//跳出函数，这里被LEV取代
        else if (op == ENT)  {*--sp = (int)bp; bp = sp; sp = sp - *pc++;}//保存当前的栈指针，同时在栈上保留一定的空间，用以存放局部变量
        else if (op == ADJ)  {sp = sp + *pc++;}//将调用子函数时压入栈中的数据清除
        else if (op == LEV)  {sp = bp; bp = (int *)*sp++; pc = (int *)*sp++;}//退出函数
        else if (op == LEA)  {ax = (int)(bp + *pc++);}//调用函数参数
            //运算符指令
            /**
                 * 每个运算符都是二元的，即有两个参数，第一个参数放在栈顶，第二个参数放在 ax 中。
                 * 这个顺序要特别注意。因为像 -，/之类的运算符是与参数顺序有关的。
                 * 计算后会将栈顶的参数退栈，结果存放在寄存器 ax中。
                 * 因此计算结束后，两个参数都无法取得了
                 * */
        else if (op == OR)  ax = *sp++ | ax;
        else if (op == XOR) ax = *sp++ ^ ax;
        else if (op == AND) ax = *sp++ & ax;
        else if (op == EQ)  ax = *sp++ == ax;
        else if (op == NE)  ax = *sp++ != ax;
        else if (op == LT)  ax = *sp++ < ax;
        else if (op == LE)  ax = *sp++ <= ax;
        else if (op == GT)  ax = *sp++ >  ax;
        else if (op == GE)  ax = *sp++ >= ax;
        else if (op == SHL) ax = *sp++ << ax;
        else if (op == SHR) ax = *sp++ >> ax;
        else if (op == ADD) ax = *sp++ + ax;
        else if (op == SUB) ax = *sp++ - ax;
        else if (op == MUL) ax = *sp++ * ax;
        else if (op == DIV) ax = *sp++ / ax;
        else if (op == MOD) ax = *sp++ % ax;
            //内置函数
        else if (op == EXIT) { printf("exit(%d)", *sp); return *sp;}
        else if (op == OPEN) { ax = open((char *)sp[1], sp[0]); }
        else if (op == CLOS) { ax = close(*sp);}
        else if (op == READ) { ax = read(sp[2], (char *)sp[1], *sp); }
        else if (op == PRTF) { tmp = sp + pc[1]; ax = printf((char *)tmp[-1], tmp[-2], tmp[-3], tmp[-4], tmp[-5], tmp[-6]); }
        else if (op == MALC) { ax = (int)malloc(*sp);}
        else if (op == MSET) { ax = (int)memset((char *)sp[2], sp[1], *sp);}
        else if (op == MCMP) { ax = memcmp((char *)sp[2], (char *)sp[1], *sp);}
        else {
            printf("unknown instruction:%d\n", op);
            return -1;
        }
    }
}

int main(int argc, char **argv){
    //因为主程序也包含在命令行的参数中，所以减少参数个数，跳过主程序
    argc--;argv++;
    int i, fd;//fd用于接受打开文件的地址
    int *tmp;//i和*tmp都是临时变量

    //在命令行中加参数-s，开启分部模式,**argv等价于(*argv)[0],将第一个字符串再次解析
    if (argc > 0 && **argv == '-' && (*argv)[1] == 's') {
        assembly = 1;
        --argc;
        ++argv;
    }
    //在命令行中加参数-d，开启调试模式
    if (argc > 0 && **argv == '-' && (*argv)[1] == 'd') {
        debug = 1;
        --argc;
        ++argv;
    }
    //未输入要运行的文件
    if (argc < 1) {
        printf("usage: xc [-s] [-d] file ...\n");
        return -1;
    }

    poolsize=256*1024;//定义内存大小
    line=1;//行号为1


    //分配内存
    if (!(text = malloc(poolsize))) {
        printf("could not malloc(%d) for text area\n", poolsize);
        return -1;
    }
    if (!(data = malloc(poolsize))) {
        printf("could not malloc(%d) for data area\n", poolsize);
        return -1;
    }
    if (!(stack = malloc(poolsize))) {
        printf("could not malloc(%d) for stack area\n", poolsize);
        return -1;
    }
    if (!(symbols=malloc(poolsize))){
        printf("could not malloc(%d) for symbol table\n",poolsize);
        return -1;
    }
    //初始化分配的内存
    memset(text, 0, poolsize);
    memset(data, 0, poolsize);
    memset(stack, 0, poolsize);
    memset(symbols, 0, poolsize);
    old_text=text;

    //向symbol table中插入内置的关键字和函数,这里的顺序与表中的顺序应一致
    //用字符串常量的第一个字符的地址赋值给指针变量src
    src = "char else enum if int return sizeof while "
          "open read close printf malloc memset memcmp exit void main";
    i=Char;
    while (i<=While){
        next();//此时返回的current_id[Token]=Id，所以下面修改为i
        current_id[Token]=i++;
    }
    i=OPEN;
    while (i<=EXIT){
        next();//和上面一样，但作为函数，还需要设置Class、Type、Value
        current_id[Class]=Sys;//区别自定义函数的系统内置函数
        current_id[Type]=INT;
        current_id[Value]=i++;
    }
    next();current_id[Token]=Char;//此时Token为void，这里将void设为char
    next();idmain=current_id;//此时指针current_id指向标识符表的main，跟踪主函数

    //以只读方式打开文件，分配src内存并读取源码
    if ((fd=open(*argv,0))<0){
        printf("could not open(%s)\n",*argv);
        return -1;
    }
    if (!(src=old_src=malloc(poolsize))){
        printf("could not malloc(%d) for source area\n",poolsize);
        return -1;
    }
    if ((i=read(fd,src,poolsize-1))<=0){
        printf("read() returned %d\n",i);
        return -1;
    }
    src[i]=0;//添加EOF结束符
    close(fd);
    //printf("hello Jessica!\n");
    //语法分析器驱动词法分析器，从后者“拉”（pull）出单词（token）来
    program();
    /**
     * 词法分析在遇到标识符的时候就会向符号表插入一个新的项，
     * 此时只填入了hash、名字以及token类型（Id）等信息。
     * 然后在对声明做语法分析时，会把该标识符所代表的实体具体的属性信息填到符号表，
     * 例如类型是int还是char、存储类别（storage class）是全局还是局部、
     * 全局变量或局部变量的下标——这就是变量的存储空间分配，函数定义对应的字节码在代码区里面的偏移量，等等
     * */
    //printf("hello krystal!\n");
    //从主函数开始加载，使pc指向主函数
    if (!(pc=(int*)idmain[Value])){
        printf("main() not defined\n");
        return -1;
    }

    //分部模式到此结束,只进行编译,不进入虚拟机实际运行汇编程序
    if (assembly){
        return 0;
    }

    //设置栈指针
    sp=(int*)((int)stack+poolsize);//从最后开始往前扩大
    *--sp=EXIT  ;//当主函数return后，调用exit
    *--sp=PUSH;tmp=sp;
    *--sp=argc;*--sp=(int)argv;
    *--sp=(int)tmp;//将命令行参数入栈

    return eval();
}