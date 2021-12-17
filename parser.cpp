class Parser
{
    string input;    // analizowany tekst
    size_t position; // bieżący znak
 
  public:
 
    Parser(string input);
    
    void skip_whitespace();
 
    char look_ahead();
 
    Expression* parse_Expression(); // wyrażenie
    Expression* parse_Constant();   // liczba
    Expression* parse_Variable();   // nazwa zmiennej
    Expression* parse_Section();    // blok
    Expression* parse_Bracket();    // nawias
    Expression* parse_String();     // napis
};

Expression* Parser::parse_Expression()
{
  Expression* e = parse_Constant();
  if (look_ahead() == EOS)
    return e;
  else
  {
    delete e;
    throw Not_parsed();
  }
}

Expression* Parser::parse_Constant()    // liczba
{
  Expression* e = parse_Variable();     // zmienna
  if (look_ahead() == EOS)
    return e;
  else
  {
    delete e;
    throw Not_parsed();
  }
}


Expression* Parser::parse_Variable()    // zmienna
{
  string s;
  while (isalnum(input[position]))
  {
    s.push_back(input[position]);
    position++;
  }
  return new Variable(s);
}


Expression* Parser::parse_Section() // blok
{
  Expression* e = NULL;
  //sprawdź nazwę zmiennej
  //sprawdź wyrażenie
  //sprawdź, czy są kolejne nawiasy
  // jeśli znaleziono '{' - sprawdź, czy będzie '}', jesli będzie kolejne '{', to przejdź do kolejnego warunku
  
  try
  {
    e = parse_Brackets();
    char c = look_ahead();
 
    while(c != '}')
    {
      position++;
      e = new Binary_operator(c, e, parse_Brackets());
      c = look_ahead();
      
      //jeśli 
    }
  }
  catch (Not_parsed)
  {
    delete e;
    throw Not_parsed();
  }
 
  return e;
}

if (look_ahead() == ')')
  {
    position++;                                     //////////// niegotowe
    return e;
  }
  else
  {
    delete e;
    throw Not_parsed();
  }
  
  
  
Expression* Parser::parse_Brackets()  // nawiasy
{
  position++; 
  Expression* e = parse_sum();
  if (look_ahead() == ')')
  {
    position++;
    return e;
  }
  else
  {
    delete e;
    throw Not_parsed();
  }
}

Expression* Parser::parse_String()
{
  char *token;
  char *rest = str;
  
  if ((token = strtok_r(rest, " ", &rest))){
        printf("%s\n", token);
  }
  else{
    delete token;
    throw Not_parsed();
  }
 
  return token;
}

Expression* Parser::parse_String_v2()
{
    string *str;
    char *ptr;
    ptr = strtok(str, " , ");
    while (ptr != NULL)  
    {  
        cout << ptr  << endl;  
        ptr = strtok (NULL, " , ");  
    }  
    return 0;  
 }

Expression* Parser::parse_Braces() // nawiasy klamrowe
{
position++;

  int brace = 1;
  Expression* e = parse_Variable();
  if (look_ahead() == '}')
  {
    brace--;
    position++;
    return e;
  }
  else if (look_ahead() == '{')
  {
    brace++;
    position++;
  }
  else
  {
    delete e;
    throw Not_parsed();
  }
}

Expression* Parser::parse_Braces_v2() // nawiasy klamrowe
{
Expression* e = parse_Constant();

int openBrackets = e.replaceAll("{", "").length();
int closeBrackets = e.replaceAll("}", "").length();

	if (openBrackets != closeBrackets) { 
	    cout << "Nawiasy klamrowe nie są domknięte!" << endl;
        return 1;
	} else {
	
        return 0;
    } 
}
