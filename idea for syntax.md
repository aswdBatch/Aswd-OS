AX language syntax examples:

these are examples of what type of syntax i want AX to have.

code1:
goal: Echo hello world
---------------------------------
print(hello world)
---------------------------------

code2:
goal: print a preset variable
---------------------------------
set <message> = "this is an example"

print (<message>)
---------------------------------

code3:
goal: print a user defined value
---------------------------------
clearvar <inputvar>

input <inputvar> "input a variable:"
print(<inputvar>)
---------------------------------

code4:
goal: do a terminal command in the dir that it is currently in
---------------------------------
sys (mkdir hello_lol)
---------------------------------

code5:
goal: ask user if they want to make a directory
---------------------------------
print (do you want to make a dir?)
confirm
if <confirm> = true
sys (mkdir hello)
print (all good!)
wait 5
exit
if <confirm> = false
print (didnt do nothing)
wait 5
exit
---------------------------------

