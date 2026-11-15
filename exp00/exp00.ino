int number =0;
void setup() {
  // initialize digital pin LED_BUILTIN as an output.
  pinMode(13, OUTPUT);
  pinMode(12, OUTPUT);
  pinMode(11, OUTPUT);
  pinMode(10, OUTPUT);
  pinMode(9, OUTPUT);
  pinMode(8, OUTPUT);
  pinMode(7, OUTPUT);
  pinMode(6, INPUT);
}
void zero(){
  digitalWrite(7, HIGH);
  digitalWrite(8, HIGH);
  digitalWrite(9, HIGH);
  digitalWrite(10, HIGH);
  digitalWrite(11, HIGH);
  digitalWrite(12, HIGH);
}
void one(){
  digitalWrite(8, HIGH);
  digitalWrite(9, HIGH);
}
void two(){
  digitalWrite(7, HIGH);
  digitalWrite(8, HIGH);
  digitalWrite(13, HIGH);
  digitalWrite(11, HIGH);
  digitalWrite(10, HIGH);
}
void three(){
  digitalWrite(7, HIGH);
  digitalWrite(8, HIGH);
  digitalWrite(9, HIGH);
  digitalWrite(13, HIGH);
  digitalWrite(10, HIGH);
}
void four(){
  digitalWrite(12, HIGH);
  digitalWrite(8, HIGH);
  digitalWrite(9, HIGH);
  digitalWrite(13, HIGH);
}
void five(){
  digitalWrite(7, HIGH);
  digitalWrite(12, HIGH);
  digitalWrite(13, HIGH);
  digitalWrite(9, HIGH);
  digitalWrite(10, HIGH);
}
void six(){
  digitalWrite(7, HIGH);
  digitalWrite(12, HIGH);
  digitalWrite(9, HIGH);
  digitalWrite(10, HIGH);
  digitalWrite(11, HIGH);
  digitalWrite(13, HIGH);
}
void seven(){
  digitalWrite(7, HIGH);
  digitalWrite(8, HIGH);
  digitalWrite(9, HIGH);
}
void eight(){
  digitalWrite(7, HIGH);
  digitalWrite(8, HIGH);
  digitalWrite(9, HIGH);
  digitalWrite(10, HIGH);
  digitalWrite(11, HIGH);
  digitalWrite(12, HIGH);
  digitalWrite(13, HIGH);
}
void nine(){
  digitalWrite(7, HIGH);
  digitalWrite(8, HIGH);
  digitalWrite(9, HIGH);
  digitalWrite(10, HIGH);
  digitalWrite(12, HIGH);
  digitalWrite(13, HIGH);
}
void loop() {
  delay(100);
  int buttonState = digitalRead(6);
  if(buttonState == HIGH){
    number=(number+1)%10;
    for(int i=7;i<14;i++){digitalWrite(i, LOW);}
    delay(50);
    if(number==0){zero();}
    else if(number==1){one();}
    else if(number==2){two();}
    else if(number==3){three();}
    else if(number==4){four();}
    else if(number==5){five();}
    else if(number==6){six();}
    else if(number==7){seven();}
    else if(number==8){eight();}
    else if(number==9){nine();}
    delay(1000);
  }
}