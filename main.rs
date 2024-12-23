use std::collections::HashMap;
use std::io;

type POSITION = (i32, i32);
type ENERGY = (i32, i32, i32, i32);

#[derive(PartialEq)]
enum OrganType {
    BASIC,
    HARVESTER,
    TENTACLE,
    SPORER,
    ROOT
}

impl OrganType {
    fn costs(&self) -> (i32, i32, i32, i32) {
        match self {
            OrganType::BASIC => (1, 0, 0, 0),
            OrganType::HARVESTER => (0, 0, 1, 1),
            OrganType::TENTACLE => (0, 1, 1, 1),
            OrganType::SPORER => (0, 1, 0, 1),
            OrganType::ROOT => (1, 1, 1, 1),
        }
    }

    fn verify(&self, organ_type_name: &str, energy: ENERGY) -> bool {
        let required_costs = self.costs(); // Get the cost of the organ type

        match organ_type_name {
            "BASIC" if *self == (OrganType::BASIC) => {
                energy.0 >= required_costs.0 
            }
            "HARVESTER" if *self == OrganType::HARVESTER => {
                energy.2 >= required_costs.2 && energy.3 >= required_costs.3 
            }
            "TENTACLE" if *self == OrganType::TENTACLE => {
                energy.1 >= required_costs.1 && energy.2 >= required_costs.2 && energy.3 >= required_costs.3
            }
            "SPORER" if *self == OrganType::SPORER => {
                energy.0 >= required_costs.0 && energy.1 >= required_costs.1 * 2 &&
                energy.2 >= required_costs.2 && energy.3 >= required_costs.3 * 2
            }
            "ROOT" if *self == OrganType::ROOT => {
                energy.0 >= required_costs.0 && energy.1 >= required_costs.1 &&
                energy.2 >= required_costs.2 && energy.3 >= required_costs.3
            }
            _ => false,
        }
    }
}

macro_rules! parse_input {
    ($x:expr, $t:ident) => {
        $x.trim().parse::<$t>().unwrap()
    };
}

fn search_for_root_basic_or_a_in_owner(
    store_positions_type: &HashMap<POSITION, String>,
    store_owner_organ: &HashMap<POSITION, i32>
) -> Vec<POSITION> {
    let mut root_basic_or_a_positions = Vec::new();
    
    // Iterate over the HashMap and find positions where the value is ROOT, BASIC, or "A"
    for (&(x, y), value) in store_positions_type.iter() {
        if (value == "ROOT" || value == "BASIC") && store_owner_organ.contains_key(&(x, y)) {
            root_basic_or_a_positions.push((x, y));
        }
    }
    
    root_basic_or_a_positions
}

//**************************************************************************
//TODO:
/*
- Check the available protein source
- Decide the proteins that I should get
- Check if the SPORER is need based on the position of the root and the protein
- 
*/
//**************************************************************************

// This function is responsible for finding all my root organs
fn root_organs_positions(
    store_positions_type: &HashMap<POSITION, String>,
    store_owner_organ: &HashMap<POSITION, i32>,

) -> Vec<POSITION> {
    let mut root_organ_position: Vec<POSITION> = Vec::new();
    for (&(x, y), value) in store_positions_type.iter() {
        if value == "ROOT" && store_owner_organ.contains_key(&(x, y)) {
            root_organ_position.push((x, y));
        }
    }
    
    root_organ_position
}

// This function return the direction to target
fn calculate_direction(current: POSITION, target: POSITION) -> String {
    if current.0 == target.0 {
        if current.1 < target.1 {
            "S".to_string()
        } else {
            "N".to_string()
        }
    } else if current.0 < target.0 {
        "E".to_string()
    } else {
        "W".to_string()
    }
}


// THis will calculate the nearest organ to protein
fn find_nearest_organ(
    organ_position: &Vec<POSITION>,
    protein_position: POSITION
)-> i32 {
    let mut lowest_distance = std::i32::MAX;
    for &(x, y) in organ_position {
        let distance = manhattan_distance(protein_position, (x, y));
        if lowest_distance > distance {
            lowest_distance = distance;
        }
    }

    lowest_distance
}

// This function will propose the protein that I should go to
fn proposed_proteins(
    store_positions_type: &HashMap<POSITION, String>,
    my_organ_position: &Vec<POSITION>,
    opp_organ_position: &Vec<POSITION>,
) -> Vec<POSITION> {
    let mut proteins: Vec<POSITION> = Vec::new();

    for (&(x, y), value) in store_positions_type.iter() {
        if value == "A" || value == "B" || value == "C" || value == "D" {

            if find_nearest_organ(my_organ_position, (x, y)) <= find_nearest_organ(opp_organ_position, (x, y)) {
                proteins.push((x, y));
            }
        }
    }

    proteins
}

// This function will return the root id of a specific organ

// This function will check the root of a specific organ
fn check_root_id(
    store_owner_organ: &HashMap<POSITION, i32>,
    organ_positon: POSITION
) -> i32 {
    store_owner_organ.iter()
        .find_map(|(&key, &val)| {if key.0 == organ_positon.0 && key.1 == organ_positon.1 { Some(val) } else { None }})
        .unwrap_or(-1)
}

// Function that check whether sporer is needed or not
fn is_sporer_needed(
    store_positions_type: &HashMap<POSITION, String>,
    my_organ_position: &Vec<POSITION>,
    proposed_protein_position: &Vec<POSITION>,
    protein_energy: ENERGY
) -> bool {
    let directions = [(0, -1), (0, 1), (-1, 0), (1, 0)];
    let type_organ: OrganType = OrganType::SPORER;

    for &(protein_x, protein_y) in proposed_protein_position {
        for &(organ_x, organ_y) in my_organ_position {
            for &(dx, dy) in &directions {
                let new_x = organ_x + dx;
                let new_y = organ_y + dy;
                // This condition to check that the the grid position is free
                if !store_positions_type.contains_key(&(new_x, new_y)) {
                    // eprintln!("at least it was in this condition {:?} {:?}", protein_x - 1 == new_x, (new_y - protein_y).abs() > 3);
                    if (protein_x - 1 == new_x || protein_x + 1 == new_x) && (new_y - protein_y).abs() > 3 && type_organ.verify("SPORER", protein_energy) {
                        // eprintln!("The function of sporer need! on x is true");
                        return true;
                    } else if (protein_y - 1 == new_y || protein_y + 1 == new_y) && (new_x - protein_x).abs() > 3 && type_organ.verify("SPORER", protein_energy) {
                        // eprintln!("The function of sporer need! on y is true");
                        return true;
                    }
                    // eprintln!("at least it was in this condition 2 {:?} {:?}", protein_y - 1 == new_y, (new_x - protein_x).abs() > 3);
                }
            }
        }
    }

    false
}

// Function that return the position of sporer
fn sporer_position(
    store_positions_type: &HashMap<POSITION, String>,
    my_organ_position: &Vec<POSITION>,
    proposed_protein_position: &Vec<POSITION>
) -> Option<(POSITION, POSITION, String)> {
    let directions = [(0, -1), (0, 1), (-1, 0), (1, 0)];

    for &(protein_x, protein_y) in proposed_protein_position {
        for &(organ_x, organ_y) in my_organ_position {
            for &(dx, dy) in &directions {
                let new_x = organ_x + dx;
                let new_y = organ_y + dy;
                // This condition to check that the the grid position is free
                if !store_positions_type.contains_key(&(new_x, new_y)) {

                    if (protein_x - 1 == new_x || protein_x + 1 == new_x) && (new_y - protein_y).abs() > 3 {
                        if new_y - protein_y < 0 {
                            return Some(((new_x, protein_y - 1), (new_x, new_y), "S".to_string()));
                        } else if new_y - protein_y > 0 {
                            return Some(((new_x, protein_y -1), (new_x, new_y), "N".to_string()));
                        }
                    } else if (protein_y - 1 == new_y || protein_y + 1 == new_y) && (new_x - protein_x).abs() > 3 {
                        if new_x - protein_x < 0 {
                            return Some(((protein_x -1, new_y), (new_x, new_y), "E".to_string()));
                        } else if new_x - protein_x > 0 {
                            return Some(((protein_x -1, new_y), (new_x, new_y), "W".to_string()));
                        }
                    }

                }
            }
        }
    }

    None
}


// This function is responsible for giving the id of a specific organ
fn search_for_organ_id(
    store_id_with_position: &HashMap<POSITION, i32>, 
    my_organ_position: &Vec<POSITION>,
    search: POSITION
) -> i32 {
    let mut closest_id = 0;
    let mut min_distance = std::i32::MAX;

    // Iterate over all positions in my_organ_position
    for &(x, y) in my_organ_position.iter() {
        let distance = (search.0 - x).abs() + (search.1 - y).abs(); // Manhattan distance

        // If this position is closer than previous ones, update closest_id
        if distance < min_distance {
            if let Some(&organ_id) = store_id_with_position.get(&(x, y)) {
                closest_id = organ_id;
                min_distance = distance;
            }
        }
    }

    closest_id
}

// Calculate Manhattan distance between two points
fn manhattan_distance(
    p1: POSITION, 
    p2: POSITION
) -> i32 {
    (p1.0 - p2.0).abs() + (p1.1 - p2.1).abs()
}

// This function is responsible for proposing an attack on the opponenent
fn near_opponent(
    my_organ_position: &Vec<POSITION>,
    store_positions_type: &HashMap<POSITION, String>,
    my_positions: POSITION
) -> (bool, String) {
    
    for (&(x, y), value) in store_positions_type.iter() {
        if value == "BASIC" && !my_organ_position.contains(&(x, y)) {
            let distance = manhattan_distance(my_positions, (x, y));
            if distance == 1 {
                let direction = if x == my_positions.0 {
                    if y > my_positions.1 {
                        "S"
                    } else {
                        "N"
                    }
                } else {
                    if x > my_positions.0 {
                        "E"
                    } else {
                        "W"
                    }
                };
                return (true, direction.to_string());
            }
        }
    }

    (false, "".to_string())
}

// Function to find the next position towards "A" within a given range (max_distance = 5)
fn find_next_position_towards_a(
    my_organ_position: &Vec<POSITION>,
    store_positions_type: &HashMap<POSITION, String>,
    proposed_protein_position: &Vec<POSITION>,
    harvesting: Vec<POSITION>,
    current_position: POSITION,
    max_distance: i32,
    width: i32,
    height: i32,
) -> Option<(POSITION, i32, String, POSITION, bool)> {
    // Get all "A" positions
    let mut store_position: HashMap<POSITION, String> = HashMap::new();
    let attack = false;
    for (&(a_x, a_y), a_type) in store_positions_type {
        if !harvesting.contains(&(a_x, a_y)) {
            store_position.insert((a_x, a_y), a_type.clone());
        }
    }
    // let a_positions = search_for_a_positions(&store_position);
    let mut harvest: POSITION = (-1, -1); 
    // eprintln!("a_positions: {:?}", a_positions);

    // Check for type A within range and find direction
    for &(a_x, a_y) in proposed_protein_position {
        let mut distance = manhattan_distance(current_position, (a_x, a_y));

        if distance <= max_distance && distance != 1 {
            eprintln!("that was the first condition");
            // Calculate the direction
            let direction = if a_x == current_position.0 {
                if a_y < current_position.1 {
                    "S"
                } else {
                    "N"
                }
            } else {
                if a_x > current_position.0 {
                    "E"
                } else {
                    "W"
                }
            };

            let directions = [(0, -1), (0, 1), (-1, 0), (1, 0)];
            distance = std::i32::MAX;
            let mut updated = false;
            let mut new_position = (0, 0);
            for &(dx, dy) in &directions {
                let new_x = current_position.0 + dx;
                let new_y = current_position.1 + dy;
                
                let new_distance = manhattan_distance((new_x, new_y), (a_x, a_y));
                if distance > new_distance && !store_positions_type.contains_key(&(new_x, new_y)) {
                    new_position = (new_x, new_y);
                    distance = new_distance;
                    updated = true;
                }
            }
            if updated {
                if distance == 1 {
                    harvest = (a_x, a_y);
                }
                eprintln!("that is weird {distance} {:?} {:?} {direction}", current_position, new_position);
                let check_opponent = near_opponent(my_organ_position, store_positions_type, new_position);
                if check_opponent.0 {
                    return Some((new_position, distance, check_opponent.1, harvest, check_opponent.0));
                }
                return Some((new_position, distance, direction.to_string(), harvest, attack));
            }
        }
    }

    // Fallback to default movement logic if no valid A positions within range
    if let Some((new_x, new_y)) = find_next_position_to_a(store_positions_type, current_position.0, current_position.1, width, height) {
        eprintln!("that was the second condition");
        let check_opponent = near_opponent(my_organ_position, store_positions_type, (new_x, new_y));
        if check_opponent.0 {
            return Some(((new_x, new_y), std::i32::MAX, check_opponent.1, harvest, check_opponent.0));
        }
        return Some(((new_x, new_y), std::i32::MAX, "".to_string(), harvest, attack));
    }

    for &(x, y) in my_organ_position.iter() {
        if let Some((new_x, new_y)) = find_next_position_to_a(store_positions_type, x, y, width, height) {
            eprintln!("that was the third condition");
            let check_opponent = near_opponent(my_organ_position, store_positions_type, (new_x, new_y));
            if check_opponent.0 {
                return Some(((new_x, new_y), std::i32::MAX, check_opponent.1, harvest, check_opponent.0));
            }
            return Some(((new_x, new_y), std::i32::MAX, "".to_string(), harvest, attack));
        }
    }

    None
}

// Existing function to search for "A" positions
fn search_for_a_positions(store_positions_type: &HashMap<POSITION, String>) -> Vec<POSITION> {
    let mut a_positions = Vec::new();

    // Iterate over the HashMap and find positions where the value is "A"
    for (&(x, y), value) in store_positions_type.iter() {
        if value == "A" {
            a_positions.push((x, y));
        }
    }

    a_positions
}

// Existing function to find the next available position to move
fn find_next_position_to_a(
    store_positions_type: &HashMap<POSITION, String>,
    current_x: i32,
    current_y: i32,
    width: i32,
    height: i32,
) -> Option<POSITION> {
    let directions = [(0, -1), (0, 1), (-1, 0), (1, 0)];

    // If no "A" positions found, fall back to searching for any available position
    for &(dx, dy) in &directions {
        let new_x = current_x + dx;
        let new_y = current_y + dy;
        let type_val = if let Some(v) = store_positions_type.get(&(new_x, new_y)) {
            v == "A"
        } else {
            false
        };
        eprintln!("the output of new_x: {new_x} and new_y:{new_y}");
        if new_x >= 0 && new_x < width && new_y >= 0 && new_y < height && !store_positions_type.contains_key(&(new_x, new_y)) && type_val != true {
            return Some((new_x, new_y));
        }
    }

    None
}


// Main game loop
fn main() {
    let mut input_line = String::new();
    io::stdin().read_line(&mut input_line).unwrap();
    let inputs = input_line.split(" ").collect::<Vec<_>>();
    let width = parse_input!(inputs[0], i32); // columns in the game grid
    let height = parse_input!(inputs[1], i32); // rows in the game grid
    let mut my_organ_position: Vec<POSITION> = Vec::new();
    let mut opp_organ_position: Vec<POSITION> = Vec::new();

    let mut store_owner_organ: HashMap<POSITION, i32> = HashMap::new();
    let mut actual_positions: HashMap<i32, POSITION> = HashMap::new(); 
    let mut proposed_protein_position: Vec<POSITION> = Vec::new();
    let mut sporer_position_id: HashMap<POSITION, i32> = HashMap::new();
    let mut sporer_should_be_launched: HashMap<i32, bool>= HashMap::new();
    let mut sporer_launched: HashMap<i32, bool> = HashMap::new();
    let mut turn = -1;

    let mut harvesting: Vec<POSITION> = Vec::new();
    let mut new_root: (i32, i32) = ( -1, -1);

    loop {

        let mut input_line = String::new();
        let mut store_positions_type: HashMap<POSITION, String> = HashMap::new();
        io::stdin().read_line(&mut input_line).unwrap();
        let entity_count = parse_input!(input_line, i32);
        let mut store_id_with_position: HashMap<POSITION, i32> = HashMap::new();
        let mut my_energy: ENERGY;
        let mut opp_energy: ENERGY;

        for _ in 0..entity_count as usize {
            let mut input_line = String::new();
            io::stdin().read_line(&mut input_line).unwrap();
            let inputs = input_line.split(" ").collect::<Vec<_>>();
            let x = parse_input!(inputs[0], i32);
            let y = parse_input!(inputs[1], i32);
            let _type = inputs[2].trim().to_string();
            let owner = parse_input!(inputs[3], i32);

            
            let organ_id = parse_input!(inputs[4], i32);
            let organ_dir = inputs[5].trim().to_string();
            let organ_parent_id = parse_input!(inputs[6], i32);
            let organ_root_id = parse_input!(inputs[7], i32);
            if owner == 1 {
                store_owner_organ.insert((x, y), organ_root_id );
                my_organ_position.push((x, y));
            }
            if owner == 0 {
                opp_organ_position.push((x, y));
            }
            if organ_id == organ_parent_id && !actual_positions.contains_key(&organ_id) {
                actual_positions.insert(organ_id, (x, y));
            }
            store_positions_type.insert((x, y), _type);
            store_id_with_position.insert((x, y), organ_id);
        }

        if turn == -1 {
            let root_basic_or_a_positions = search_for_root_basic_or_a_in_owner(&store_positions_type, &store_owner_organ);
            if let Some(&(x, y)) = root_basic_or_a_positions.first() {
                let id = search_for_organ_id(&store_id_with_position, &my_organ_position, (x, y));
                if !actual_positions.contains_key(&id) {
                    actual_positions.insert(id, (x, y));
                } else {
                    actual_positions.entry(id).and_modify(|val| { val.0 = x; val.1 = y; }).or_insert((x, y));
                }
            }
            proposed_protein_position = proposed_proteins(&store_positions_type, &my_organ_position, &opp_organ_position);
            turn = 0;
        }

        // INPUT for my organism's status
        let mut input_line = String::new();
        io::stdin().read_line(&mut input_line).unwrap();
        let inputs = input_line.split(" ").collect::<Vec<_>>();
        let my_a = parse_input!(inputs[0], i32);
        let my_b = parse_input!(inputs[1], i32);
        let my_c = parse_input!(inputs[2], i32);
        let my_d = parse_input!(inputs[3], i32); // your protein stock
        my_energy = (my_a, my_b, my_c, my_d);
        
        let mut input_line = String::new();
        io::stdin().read_line(&mut input_line).unwrap();
        let inputs = input_line.split(" ").collect::<Vec<_>>();
        let opp_a = parse_input!(inputs[0], i32);
        let opp_b = parse_input!(inputs[1], i32);
        let opp_c = parse_input!(inputs[2], i32);
        let opp_d = parse_input!(inputs[3], i32); // opponent's protein stock
        opp_energy = (opp_a, opp_b, opp_c, opp_d);
        
        let mut input_line = String::new();
        io::stdin().read_line(&mut input_line).unwrap();
        let required_actions_count = parse_input!(input_line, i32);
        if turn == 0 {
            for (&id, &(target_x, target_y)) in actual_positions.clone().iter() {
                if is_sporer_needed(&store_positions_type, &my_organ_position, &proposed_protein_position, my_energy) {
                    eprintln!("was here inside the condition to check sporer!");
                    sporer_launched.insert(id, false);
                    sporer_should_be_launched.insert(id, true);
                }
            }
            turn += 1;
        }

        // for _ in 0..required_actions_count as usize {
        eprintln!("the positions available: {:?}", actual_positions);
        for (&id, &(target_x, target_y)) in actual_positions.clone().iter() {
            // Check if the sporer should be launched
            eprintln!("how many did it print");
            if sporer_should_be_launched.contains_key(&id) && sporer_should_be_launched[&id] {
                if let Some(((sporer_x, sporer_y), (organ_x, organ_y), direction)) = sporer_position(&store_positions_type,&my_organ_position, &proposed_protein_position) {
                    let id_2 = search_for_organ_id(&store_id_with_position, &my_organ_position, (organ_x, organ_y));
                    new_root = (sporer_x, sporer_y);
                    sporer_position_id.insert((organ_x, organ_y), id_2);
                    eprintln!("the position sporer: x: {sporer_x} y : {sporer_y}, for organ: x {organ_x} y :{organ_y}");
                    // actual_positions.entry(id).and_modify(|val| { val.0 = organ_x; val.1 = organ_y; }).or_insert((organ_x, organ_y));
                    actual_positions.remove(&id);
                    actual_positions.insert(id_2, (organ_x, organ_y));

                    sporer_launched.entry(id).and_modify(|val: &mut bool| { *val = true;}).or_insert(true);
                    eprintln!("before removing the id: {:?}", sporer_should_be_launched);
                    sporer_should_be_launched.remove(&id);
                    eprintln!("after removing the id: {:?}", sporer_should_be_launched);
                    println!("GROW {} {} {} SPORER {}", id_2, organ_x, organ_y, direction);
                    // println!("SPORE {} {} {}", id_2, new_root.0, new_root.1);
                }
            }
            else if  sporer_launched.contains_key(&id) && sporer_launched[&id] && sporer_position_id.contains_key(&(target_x, target_y)) {
                let id_2 = search_for_organ_id(&store_id_with_position, &my_organ_position, (target_x, target_y));
                println!("SPORE {} {} {}", id_2, new_root.0, new_root.1);
                sporer_position_id.clear();
                sporer_launched.remove(&id);
            }
            else if let Some(((new_x, new_y), distance, direction, harvest, attack)) = find_next_position_towards_a(
                &my_organ_position,
                &store_positions_type,
                &proposed_protein_position,
                harvesting.clone(),
                (target_x, target_y),
                5,
                width,
                height,
            ) {
                let id_2 = search_for_organ_id(&store_id_with_position, &my_organ_position, (target_x, target_y));
                if attack {
                    // It will throw an TENTACLE command to attack nearbe
                    // actual_positions.entry(id).and_modify(|val| { val.0 = new_x; val.1 = new_y; }).or_insert((new_x, new_y));
                    actual_positions.remove(&id);
                    actual_positions.insert(id_2, (new_x, new_y));
                    println!("GROW {} {} {} TENTACLE {}", id_2, new_x, new_y, direction);
                }
                else if distance == 1 {
                    // Print special grow command when near "A"
                    // actual_positions.entry(id).and_modify(|val| { val.0 = new_x; val.1 = new_y; }).or_insert((new_x, new_y));
                    actual_positions.remove(&id);
                    actual_positions.insert(id_2, (new_x, new_y));
                    harvesting.push(harvest);
                    println!("GROW {} {} {} HARVESTER {}", id_2, new_x, new_y, direction);
                } else {
                    // actual_positions.entry(id).and_modify(|val| { val.0 = new_x; val.1 = new_y; }).or_insert((new_x, new_y));
                    actual_positions.remove(&id);
                    actual_positions.insert(id_2, (new_x, new_y));
                    println!("GROW {} {} {} BASIC", id_2, new_x, new_y);
                }
            } else {
                println!("WAIT");
            }
        }
    }
}
